#include <stdio.h>
#include "typedefs.h"
#include "smalloc.h"
#include "update.h"
#include "vec.h"
#include "macros.h"
#include "physics.h"
#include "names.h"
#include "nrnb.h"

/* 
 * This file implements temperature and pressure coupling algorithms:
 * For now only the Weak coupling and the modified weak coupling.
 *
 * Furthermore computation of pressure and temperature is done here
 *
 */

void calc_pres(matrix box,tensor ekin,tensor vir,tensor pres)
{
  int  n,m;
  real fac;

  /* Uitzoeken welke ekin hier van toepassing is, zie Evans & Morris - E. */ 
  /* Wrs. moet de druktensor gecorrigeerd worden voor de netto stroom in het */
  /* systeem...       */
  
  fac=PRESFAC*2.0/(det(box));
  for(n=0; (n<DIM); n++)
    for(m=0; (m<DIM); m++)
      pres[n][m]=(ekin[n][m]-vir[n][m])*fac;
      
#ifdef DEBUG
  pr_rvecs(stdlog,0,"pres",pres,DIM);
  pr_rvecs(stdlog,0,"ekin",ekin,DIM);
  pr_rvecs(stdlog,0,"vir ",vir, DIM);
#endif
}

real calc_temp(real ekin,int nrdf)
{
  return (2.0*ekin)/(nrdf*BOLTZ);
}

real run_aver(real old,real cur,int step,int nmem)
{
  nmem   = max(1,nmem);
  
  return ((nmem-1)*old+cur)/nmem;
}


void do_pcoupl(t_inputrec *ir,int step,tensor pres,
	       matrix box,int start,int nr_atoms,
	       rvec x[],ushort cFREEZE[],
	       t_nrnb *nrnb,rvec freezefac[])
{
  static bool bFirst=TRUE;
  static rvec PPP;
  int    n,d,m,g,ncoupl=0;
  real   scalar_pressure;
  real   X,Y,Z,dx,dy,dz;
  rvec   factor;
  tensor mu;
  real   muxx,muxy,muxz,muyx,muyy,muyz,muzx,muzy,muzz;
  real   fgx,fgy,fgz;
  
  /*
   *  PRESSURE SCALING 
   *  Step (2P)
   */
  if (bFirst) {
    /* Initiate the pressure to the reference one */
    for(m=0; (m<DIM); m++)
      PPP[m] = ir->ref_p[m];
    bFirst=FALSE;
  }
  scalar_pressure=0;
  for(m=0; (m<DIM); m++) {
    PPP[m]           = run_aver(PPP[m],pres[m][m],step,ir->npcmemory);
    scalar_pressure += PPP[m]/3.0;
  }

  if ((ir->epc != epcNO) && (scalar_pressure != 0.0)) {
    for(m=0; (m<DIM); m++)
      factor[m] = ir->compress[m]*ir->delta_t/ir->tau_p;
    clear_mat(mu);
    switch (ir->epc) {
    case epcISOTROPIC:
      for(m=0; (m<DIM); m++)
	mu[m][m] = 
	  pow(1.0-factor[m]*(ir->ref_p[m]-scalar_pressure),1.0/3.0);
      break;
    case epcANISOTROPIC:
      for (m=0; (m<DIM); m++)
	mu[m][m] = pow(1.0-factor[m]*(ir->ref_p[m] - PPP[m]),1.0/3.0);
      break;
    case epcTRICLINIC:
    default:
      fprintf(stderr,"Pressure coupling type %s not supported yet\n",
	      EPCOUPLTYPE(ir->epc));
      exit(1);
    }
#ifdef DEBUG
    pr_rvecs(stdlog,0,"mu  ",mu,DIM);
#endif
    /* Scale the positions using matrix operation */
    nr_atoms+=start;
    muxx=mu[XX][XX],muxy=mu[XX][YY],muxz=mu[XX][ZZ];
    muyx=mu[YY][XX],muyy=mu[YY][YY],muyz=mu[YY][ZZ];
    muzx=mu[ZZ][XX],muzy=mu[ZZ][YY],muzz=mu[ZZ][ZZ];
    for (n=start; (n<nr_atoms); n++) {
      g=cFREEZE[n];
      fgx=freezefac[g][XX];
      fgy=freezefac[g][YY];
      fgz=freezefac[g][ZZ];
      
      X=x[n][XX];
      Y=x[n][YY];
      Z=x[n][ZZ];
      dx=muxx*X+muxy*Y+muxz*Z;
      dy=muyx*X+muyy*Y+muyz*Z;
      dz=muzx*X+muzy*Y+muzz*Z;
      x[n][XX]=X+fgx*(dx-X);
      x[n][YY]=Y+fgy*(dy-Y);
      x[n][ZZ]=Z+fgz*(dz-Z);
      
      ncoupl++;
    }
    /* compute final boxlengths */
    for (d=0; (d<DIM); d++)
      for (m=0; (m<DIM); m++)
	box[d][m] *= mu[d][m];
  }
  inc_nrnb(nrnb,eNR_PCOUPL,ncoupl);
}

void tcoupl(bool bTC,t_grpopts *opts,t_groups *grps,
	    real dt,real SAfactor,int step,int nmem)
{
  static real *Told=NULL;
  int    i;
  real   T,reft,lll;

  if (!Told) {
    snew(Told,opts->ngtc);
    for(i=0; (i<opts->ngtc); i++) 
      Told[i]=opts->ref_t[i]*SAfactor;
  }
  
  for(i=0; (i<opts->ngtc); i++) {
    reft=opts->ref_t[i]*SAfactor;
    if (reft < 0)
      reft=0;
    
    Told[i] = run_aver(Told[i],grps->tcstat[i].T,step,nmem);
    T       = Told[i];
    
    if ((bTC) && (T != 0.0)) {
      lll=sqrt(1.0 + (dt/opts->tau_t[i])*(reft/T-1.0));
      grps->tcstat[i].lambda=max(min(lll,1.25),0.8);
    }
    else
      grps->tcstat[i].lambda=1.0;
#ifdef DEBUGTC
    fprintf(stdlog,"group %d: T: %g, Lambda: %g\n",
	    i,T,grps->tcstat[i].lambda);
#endif
  }
}


