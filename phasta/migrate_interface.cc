#include "phAttrib.h"
#include "phBC.h"
#include <ph.h>
#include <PCU.h>
#include <apf.h>
#include <apfMesh2.h>
#include <apfMDS.h>
#include <gmi_mesh.h>
#include <gmi_sim.h>
#include <SimUtil.h>
#include <cassert>
#include <cstdlib>
#include <iostream>

namespace
{
  const char* modelFile = 0;
  const char* inMesh = 0;
  const char* outMesh = 0;
  char const* attribFile = 0;

void freeMesh(apf::Mesh* m)
{
  m->destroyNative();
  apf::destroyMesh(m);
}

void getConfig(int argc, char** argv)
{
  if (argc < 4 || argc > 5) {
    if ( !PCU_Comm_Self() ) {
      fprintf(stderr,"Usage: %s <model .x_t> <attributes .smd> <in mesh> <out mesh>\n", argv[0]);
      fprintf(stderr,"       to take model and attributes in separate files\n");
      fprintf(stderr,"Usage: %s <model+attributes .smd> <in mesh> <out mesh>\n", argv[0]);
      fprintf(stderr,"       to take combined model and attributes file (by simTranslate)\n");}
    MPI_Finalize();
    exit(EXIT_FAILURE);
  }
  if (argc == 5) {
    modelFile = argv[1];
    attribFile = argv[2];
    inMesh = argv[3];
    outMesh = argv[4];}
  else if (argc == 4) {
    attribFile = argv[1];
    inMesh = argv[2];
    outMesh = argv[3];
    modelFile = argv[4];}
}
}

namespace ph
{

static bool isInterface(gmi_model* gm, gmi_ent* ge, FieldBCs& fbcs)
{
  int d = gmi_dim(gm, ge);
  if (d > 2)
    return false;
  if (d == 2)
    return getBCValue(gm, fbcs, ge) != 0;
  bool out = false;
  gmi_set* s = gmi_adjacent(gm, ge, d + 1);
  for (int i = 0; i < s->n; ++i)
    if (isInterface(gm, s->e[i], fbcs)) {
      out = true;
      break;
    }
  gmi_free_set(s);
  return out;
}
}

int main(int argc, char** argv)
{
  MPI_Init(&argc,&argv);
  PCU_Comm_Init();
  SimUtil_start();
  Sim_readLicenseFile(0);
  gmi_sim_start();
  gmi_register_mesh();
  gmi_register_sim();

  getConfig(argc,argv);

  gmi_model* gm;
  gm = gmi_sim_load(modelFile, attribFile);
  apf::Mesh2* m = apf::loadMdsMesh(gm, inMesh);
  m->verify();
  ph::BCs bcs;
  ph::getSimmetrixAttributes(gm, bcs);
  std::string name("DG interface");
  if (!haveBC(bcs, name))
    ph::fail("no DG interface attributes!");
  ph::FieldBCs& fbcs = bcs.fields[name];

  int faceDim = m->getDimension() - 1;

  apf::MeshIterator* it = m->begin(faceDim);
  apf::MeshEntity* f;
  apf::Migration* plan = new apf::Migration(m);
  apf::Parts residence;

/*
apf::MeshEntity* v;
apf::MeshIterator* it2 = m->begin(faceDim-1);
while ((v = m->iterate(it2))) {
  if (m->isOwned(v)) printf("proc-%d: yes\n",PCU_Comm_Self());
  else printf("proc-%d: no\n",PCU_Comm_Self())
}
 */

  int nDG = 0;
  while ((f = m->iterate(it))) {
    apf::ModelEntity* me = m->toModel(f);
    if (m->getModelType(me) != faceDim)
      continue;

    gmi_ent* gf = (gmi_ent*) me;
    if (!ph::isInterface(m->getModel(), gf, fbcs))
      continue;

    ++nDG;
    apf::Matches matches;
    m->getMatches(f,matches);

    apf::MeshEntity* e = m->getUpward(f, 0);

    int remoteResidence = -1;
    for (size_t j = 0; j != matches.getSize(); ++j) {
      if (matches[j].peer != PCU_Comm_Self()) 
        remoteResidence = matches[j].peer;
//printf("proc-%d: j=%d, peer=%d.\n",PCU_Comm_Self(),j,matches[j].peer);
    }

  if (remoteResidence > PCU_Comm_Self())
    plan->send(e,remoteResidence);
//printf("proc-%d: %d has a remote copy on %d.\n",PCU_Comm_Self(),++i,remoteResidence);
//printf("proc-%d sending to %d\n",PCU_Comm_Self(),plan->sending(e));
  }
  m->end(it);
  printf("proc-%d: number of migrating elements: %d\n",PCU_Comm_Self(),plan->count());
/*
for (int i = 0; i < plan->count(); ++i) 
  printf("proc-%d: sending %d to %d:\n",PCU_Comm_Self(),i,plan->sending(plan->get( i )));
 */

  m->migrate(plan);
  m->verify();
  m->writeNative(outMesh);
  apf::writeVtkFiles("test", m);
  freeMesh(m);

  gmi_sim_stop();
  Sim_unregisterAllKeys();
  SimUtil_stop();
  PCU_Comm_Free();
  MPI_Finalize();
}
