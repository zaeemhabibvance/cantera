/**
 * @file vcs_rxnadj.cpp
 *  routines for carrying out various line adjustments
 */
/*
 *  $Id$
 */
/*
 * Copywrite (2006) Sandia Corporation. Under the terms of
 * Contract DE-AC04-94AL85000 with Sandia Corporation, the
 * U.S. Government retains certain rights in this software.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "vcs_solve.h"
#include "vcs_internal.h" 
#include "vcs_VolPhase.h"

namespace VCSnonideal {

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/

int VCS_SOLVE::vcs_rxn_adj_cg(void)
   
  /**************************************************************************
   *
   * vcs_rxn_adj_cg:
   *
   * Calculates reaction adjustments. This does what equation 6.4-16, p. 143 
   * in Smith and Missen is suppose to do. However, a full matrix is
   * formed and then solved via a conjugate gradient algorithm. No 
   * preconditioning is done.
   *
   * If special branching is warranted, then the program bails out.
   *
   * Output 
   * ------- 
   * DS(I) : reaction adjustment, where I refers to the Ith species
   * Special branching occurs sometimes. This causes the component basis 
   * to be reevaluated 
   *     return = 0 : normal return
   *              1 : A single species phase species has been zeroed out
   *                  in this routine. The species is a noncomponent 
   *              2 : Same as one but, the zeroed species is a component. 
   *
   * Special attention is taken to flag cases where the direction of the
   * update is contrary to the steepest descent rule. This is an important
   * attribute of the regular vcs algorithm. We don't want to violate this
   ***************************************************************************/
{  
  int irxn, j;
  int k = 0;
  int kspec, soldel = 0;
  double s, xx, dss;
  double *dnPhase_irxn;
#ifdef DEBUG_MODE
  char ANOTE[128];
  plogf("   "); for (j = 0; j < 77; j++) plogf("-");
  plogf("\n   --- Subroutine rxn_adj_cg() called\n");
  plogf("   --- Species         Moles   Rxn_Adjustment | Comment\n");
#endif

  /*
   *   Precalculation loop -> we calculate quantities based on
   *   loops over the number of species. 
   *   We also evaluate whether the matrix is appropriate for
   *   this algorithm. If not, we bail out.
   */
  for (irxn = 0; irxn < m_numRxnRdc; ++irxn) {
#ifdef DEBUG_MODE
    sprintf(ANOTE,"Normal Calc");
#endif
     
    kspec = ir[irxn];
    dnPhase_irxn = DnPhase[irxn];
      
    if (soln[kspec] == 0.0 && (! SSPhase[kspec])) {
      /* *******************************************************************/
      /* **** MULTISPECIES PHASE WITH total moles equal to zero ************/
      /* *******************************************************************/
      /*
       *       HKM -> the statment below presupposes units in dg[]. It probably
       *              should be replaced with something more relativistic
       */
      if (dg[irxn] < -1.0e-4) {
#ifdef DEBUG_MODE
	(void) sprintf(ANOTE, "MultSpec: come alive DG = %11.3E", dg[irxn]);       
#endif
	ds[kspec] = 1.0e-10;
	spStatus[irxn] = VCS_SPECIES_MAJOR;
	--(m_numRxnMinorZeroed);
      } else {
#ifdef DEBUG_MODE
	(void) sprintf(ANOTE, "MultSpec: still dead DG = %11.3E", dg[irxn]);       
#endif
	ds[kspec] = 0.0;
      }
    } else {
      /* ********************************************** */
      /* **** REGULAR PROCESSING            ********** */
      /* ********************************************** */
      /*
       *     First take care of cases where we want to bail out
       *
       *
       *     Don't bother if superconvergence has already been achieved 
       *     in this mode.
       */
      if (fabs(dg[irxn]) <= tolmaj2) {
#ifdef DEBUG_MODE
	sprintf(ANOTE,"Skipped: converged DG = %11.3E\n", dg[irxn]);
	plogf("   --- "); plogf("%-12.12s", SpName[kspec].c_str());
	plogf("  %12.4E %12.4E | %s\n",  soln[kspec], ds[kspec], ANOTE);
#endif		    
	continue;
      }
      /*
       *     Don't calculate for minor or nonexistent species if      
       *     their values are to be decreasing anyway.                
       */
      if (spStatus[irxn] <= VCS_SPECIES_MINOR && dg[irxn] >= 0.0) {
#ifdef DEBUG_MODE
	sprintf(ANOTE,"Skipped: IC = %3d and DG >0: %11.3E\n", 
		spStatus[irxn], dg[irxn]);
	plogf("   --- "); plogf("%-12.12s", SpName[kspec].c_str());
	plogf("  %12.4E %12.4E | %s\n", soln[kspec], ds[kspec], ANOTE);
#endif		    
	continue;
      }
      /*
       *     Start of the regular processing
       */
      if (SSPhase[kspec]) s = 0.0;
      else                s = 1.0 / soln[kspec];
      for (j = 0; j < m_numComponents; ++j) {
	if (! SSPhase[j])  s += SQUARE(sc[irxn][j]) / soln[j];
      }
      for (j = 0; j < NPhase; j++) {
	if (! (VPhaseList[j])->SingleSpecies) {
	  if (TPhMoles[j] > 0.0) 
	    s -= SQUARE(dnPhase_irxn[j]) / TPhMoles[j];
	}
      }
      if (s != 0.0) {
	ds[kspec] = -dg[irxn] / s;
      } else {
	/* ************************************************************ */
	/* **** REACTION IS ENTIRELY AMONGST SINGLE SPECIES PHASES **** */
	/* **** DELETE ONE SOLID AND RECOMPUTE BASIS          ********* */
	/* ************************************************************ */
	/* 
	 *     Either the species L will disappear or one of the 
	 *     component single species phases will disappear. The sign 
	 *     of DG(I) will indicate which way the reaction will go. 
	 *     Then, we need to follow the reaction to see which species 
	 *     will zero out first. 
	 */
	if (dg[irxn] > 0.0) {
	  dss = soln[kspec];
	  k = kspec;
	  for (j = 0; j < m_numComponents; ++j) {
	    if (sc[irxn][j] > 0.0) {
	      xx = soln[j] / sc[irxn][j];
	      if (xx < dss) {
		dss = xx;
		k = j;
	      }
	    }
	  }
	  dss = -dss;
	} else {
	  dss = 1.0e10;
	  for (j = 0; j < m_numComponents; ++j) {
	    if (sc[irxn][j] < 0.0) {
	      xx = -soln[j] / sc[irxn][j];
	      if (xx < dss) {
		dss = xx;
		k = j;
	      }
	    }
	  }
	}
	/*
	 *          Here we adjust the mole fractions 
	 *          according to DSS and the stoichiometric array 
	 *          to take into account that we are eliminating 
	 *          the kth species. DSS contains the amount 
	 *          of moles of the kth species that needs to be 
	 *          added back into the component species. 
	 */
	if (dss != 0.0) {
	  soln[kspec] += dss;
	  TPhMoles[PhaseID[kspec]] +=  dss;
	  for (j = 0; j < m_numComponents; ++j) {
	    soln[j] += dss * sc[irxn][j];
	    TPhMoles[PhaseID[j]] +=  dss * sc[irxn][j];
	  }
	  soln[k] = 0.0;
	  TPhMoles[PhaseID[k]] = 0.0; 
#ifdef DEBUG_MODE
	  plogf("   --- vcs_st2 Special section to delete ");
	  plogf("%-12.12s", SpName[k].c_str());
	  plogf("\n   ---   Immediate return - Restart iteration\n");
#endif
	  /*
	   *            We need to immediately recompute the 
	   *            component basis, because we just zeroed 
	   *            it out. 
	   */
	  if (k != kspec) soldel = 2;
	  else            soldel = 1;
	  return soldel;
	}
      }
    } /* End of regular processing */
#ifdef DEBUG_MODE
    plogf("   --- "); plogf("%-12.12s", SpName[kspec].c_str());
    plogf("  %12.4E %12.4E | %s\n", soln[kspec], ds[kspec], ANOTE);
#endif	
  } /* End of loop over non-component stoichiometric formation reactions */
   

   
   
  /*
   *
   *     When we form the Hessian we must be careful to ensure that it
   * is a symmetric positive definate matrix, still. This means zeroing
   * out columns when we zero out rows as well.
   *     -> I suggest writing a small program to make sure of this
   *        property.
   */
   
   
#ifdef DEBUG_MODE
  plogf("   "); for (j = 0; j < 77; j++) plogf("-"); plogf("\n");
#endif
  return soldel;
} /* vcs_rxn_adj_cg() ********************************************************/
/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/

double VCS_SOLVE::vcs_Hessian_diag_adj(int irxn, double hessianDiag_Ideal)
   
  /**************************************************************************
   *
   * vcs_actCoeff_diag_adj(irxn):
   *
   *  Calculates the diagonal contribution to the Hessian due to 
   *  the dependence of the activity coefficients on the mole numbers.
   *
   *  (See framemaker notes, Eqn. 20 - VCS Equations document)
   *
   *  We allow the diagonal to be increased positively to any degree.
   *  We allow the diagonal to be decreased to 1/3 of the ideal solution
   *  value, but no more -> it must remain positive.
   **************************************************************************/
{
  double diag = hessianDiag_Ideal;
  double hessActCoef = vcs_Hessian_actCoeff_diag(irxn);
  if (hessianDiag_Ideal <= 0.0) {
    plogf("We shouldn't be here\n");
    exit(-1);
  }
  if (hessActCoef >= 0.0) {
    diag += hessActCoef;
  } else if (fabs(hessActCoef) < 0.6666 *  hessianDiag_Ideal) {
    diag += hessActCoef;
  } else {
    diag -= 0.6666 *  hessianDiag_Ideal;
  }
  return diag;
}
/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/

double VCS_SOLVE::vcs_Hessian_actCoeff_diag(int irxn)
   
  /**************************************************************************
   *
   * vcs_Hessian_actCoeff_diag(irxn):
   *
   *  Calculates the diagonal contribution to the Hessian due to 
   *  the dependence of the activity coefficients on the mole numbers.
   *  (See framemaker notes, Eqn. 20 - VCS Equations document)
   **************************************************************************/
{
  int kspec, k, l, kph;
  double s;
  double *sc_irxn;
  kspec = ir[irxn];
  kph = PhaseID[kspec];  
  sc_irxn = sc[irxn];
  /*
   *   First the diagonal term of the Jacobian
   */
  s = dLnActCoeffdMolNum[kspec][kspec];
  /*
   *    Next, the other terms. Note this only a loop over the components
   *    So, it's not too expensive to calculate.
   */
  for (l = 0; l < m_numComponents; l++) {
    if (!SSPhase[l]) {
      for (k = 0; k < m_numComponents; ++k) { 
	if (PhaseID[k] == PhaseID[l]) { 
	  s += sc_irxn[k] * sc_irxn[l] * dLnActCoeffdMolNum[k][l];
	}
      }
      if (kph == PhaseID[l]) { 
	s += sc_irxn[l] * (dLnActCoeffdMolNum[kspec][l] + dLnActCoeffdMolNum[l][kspec]);	
      }
    }
  }
  return s;
}
/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/


void VCS_SOLVE::vcs_CalcLnActCoeffJac(const double * const moleSpeciesVCS)
 
  /*************************************************************************
   *
   *
   *
   *
   *************************************************************************/
{
  /*
   * Loop over all of the phases in the problem
   */
  for (int iphase = 0; iphase < NPhase; iphase++) {
    vcs_VolPhase *Vphase = VPhaseList[iphase];
    /*
     * We don't need to call single species phases;
     */
    if (!Vphase->SingleSpecies) {
      /*
       * update the Ln Act Coeff jacobian entries with respect to the
       * mole number of species in the phase
       */
      Vphase->updateLnActCoeffJac(moleSpeciesVCS);
      /*
       * Download the resulting calculation into the full matrix
       * -> This scatter calculation is carried out in the 
       *    volume object.
       */
      Vphase->sendToVCSLnActCoeffJac(dLnActCoeffdMolNum.baseDataAddr());
    }
  }
}

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/

double VCS_SOLVE::deltaG_Recalc_Rxn(int irxn, const double *const molNum,
				    double * const ac, double * const mu_i)
    
  /*************************************************************************
   *
   *  deltaG_Recalc_Rxn
   *       This function recalculates the deltaG for reaction irxn,
   *       given the mole numbers in molNum. It uses the temporary
   *       space mu_i, to hold the chemical potentials
   *************************************************************************/
{
  int kspec = irxn + m_numComponents;
  int *pp_ptr = PhaseParticipation[irxn];
  for (int iphase = 0; iphase < NPhase; iphase++) {
    if (pp_ptr[iphase]) {
      vcs_chemPotPhase(iphase, molNum, ac, mu_i);
    }
  }
  double deltaG = mu_i[kspec];
  double *sc_irxn = sc[irxn];
  for (int k = 0; k < m_numComponents; k++) {
    deltaG += sc_irxn[k] * mu_i[k];
  }
  return deltaG;
}

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
#ifdef DEBUG_MODE
double VCS_SOLVE::vcs_line_search(int irxn, double dx_orig, char *ANOTE)
#else
double VCS_SOLVE::vcs_line_search(int irxn, double dx_orig)
#endif
  /*************************************************************************
   *
   *  In this routine we carry out a rough line search algorithm 
   *  to make sure that the dG doesn't switch signs prematurely.
   *
   *
   *************************************************************************/
{
  int its = 0;
  int k;
  int kspec = ir[irxn];
  const int MAXITS = 10;
  double dx = dx_orig;
  double *sc_irxn = sc[irxn];
  double *molNumBase = VCS_DATA_PTR(soln);
  double *acBase = VCS_DATA_PTR(ActCoeff0);
  double *ac = VCS_DATA_PTR(ActCoeff);
  double *molNum = VCS_DATA_PTR(wt);
  double molSum = 0.0;
  double slope;
  /*
   * Calculate the deltaG value at the dx = 0.0 point
   */
  double deltaGOrig = deltaG_Recalc_Rxn(irxn, molNumBase, acBase, 
					VCS_DATA_PTR(m_feSpecies_old));
  double forig = fabs(deltaGOrig) + 1.0E-15;
  if (deltaGOrig > 0.0) {
    if (dx_orig > 0.0) {
      dx = 0.0;
#ifdef DEBUG_MODE
      if (vcs_debug_print_lvl >= 2) {
	//plogf("    --- %s :Warning possible error dx>0 dg > 0\n", SpName[kspec]);
      }
      sprintf(ANOTE,"Rxn reduced to zero step size in line search: dx>0 dg > 0");
#endif
      return dx;
    }
  } else if (deltaGOrig < 0.0) {
    if (dx_orig < 0.0) {
      dx = 0.0;
#ifdef DEBUG_MODE
      if (vcs_debug_print_lvl >= 2) {
	//plogf("   --- %s :Warning possible error dx<0 dg < 0\n", SpName[kspec]);
      }
      sprintf(ANOTE,"Rxn reduced to zero step size in line search: dx<0 dg < 0");
#endif
      return dx;
    }
  } else if (deltaGOrig == 0.0) {
    return 0.0;
  }
  if (dx_orig == 0.0) return 0.0;

  vcs_dcopy(molNum, molNumBase, m_numSpeciesRdc);
  molSum =  molNumBase[kspec];
  molNum[kspec] = molNumBase[kspec] + dx_orig;
  for (k = 0; k < m_numComponents; k++) {
    molNum[k] = molNumBase[k] + sc_irxn[k] * dx_orig;
    molSum +=  molNumBase[k];
  }

  double deltaG1 = deltaG_Recalc_Rxn(irxn, molNum, ac, VCS_DATA_PTR(m_feSpecies_new));
    
  /*
   * If deltaG hasn't switched signs when going the full distance
   * then we are heading in the appropriate direction, and
   * we should accept the current full step size
   */
  if (deltaG1 * deltaGOrig > 0.0) {
    dx = dx_orig;
    goto finalize;
  }
  /*
   * If we have decreased somewhat, the deltaG return after finding
   * a better estimate for the line search.
   */
  if (fabs(deltaG1) < 0.8*forig) {
    if (deltaG1 * deltaGOrig < 0.0) {
      slope = (deltaG1 - deltaGOrig) / dx_orig;
      dx = -deltaGOrig / slope;
    } else {
      dx = dx_orig;
    }
    goto finalize;
  }
   
  dx = dx_orig;
   
  for (its = 0; its < MAXITS; its++) {
    /*
     * Calculate the approximation to the total Gibbs free energy at
     * the dx  *= 0.5 point
     */
    dx *= 0.5;
    molNum[kspec] = molNumBase[kspec] + dx;
    for (k = 0; k < m_numComponents; k++) {
      molNum[k] = molNumBase[k] + sc_irxn[k] * dx;
    }
    double deltaG = deltaG_Recalc_Rxn(irxn, molNum, ac, VCS_DATA_PTR(m_feSpecies_new));
    /*
     * If deltaG hasn't switched signs when going the full distance
     * then we are heading in the appropriate direction, and
     * we should accept the current step
     */
    if (deltaG * deltaGOrig > 0.0) {
      goto finalize;
    }
    /*
     * If we have decreased somewhat, the deltaG return after finding
     * a better estimate for the line search.
     */
    if (fabs(deltaG) / forig < (1.0 - 0.1 * dx / dx_orig)) {
      if (deltaG * deltaGOrig < 0.0) {
	slope = (deltaG - deltaGOrig) / dx;
	dx = -deltaGOrig / slope;
      }
      goto finalize;
    }
  }

 finalize:
  if (its >= MAXITS) {
#ifdef DEBUG_MODE
    sprintf(ANOTE,"Rxn reduced to zero step size from %g to %g (MAXITS)", 
	    dx_orig, dx);
    return dx;
#endif
  }
#ifdef DEBUG_MODE
  if (dx != dx_orig) {
    sprintf(ANOTE,"Line Search reduced step size from %g to %g",
	    dx_orig, dx);
  }
#endif

  return dx;
}
/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
}

