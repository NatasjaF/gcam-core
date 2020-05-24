/*
* LEGAL NOTICE
* This computer software was prepared by Battelle Memorial Institute,
* hereinafter the Contractor, under Contract No. DE-AC05-76RL0 1830
* with the Department of Energy ( DOE ). NEITHER THE GOVERNMENT NOR THE
* CONTRACTOR MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR ASSUMES ANY
* LIABILITY FOR THE USE OF THIS SOFTWARE. This notice including this
* sentence must appear on any copies of this computer software.
* 
* EXPORT CONTROL
* User agrees that the Software will not be shipped, transferred or
* exported into any country or used in any manner prohibited by the
* United States Export Administration Act or any other applicable
* export laws, restrictions or regulations (collectively the "Export Laws").
* Export of the Software may require some form of license or other
* authority from the U.S. Government, and failure to obtain such
* export control license may result in criminal liability under
* U.S. laws. In addition, if the Software is identified as export controlled
* items under the Export Laws, User represents and warrants that User
* is not a citizen, or otherwise located within, an embargoed nation
* (including without limitation Iran, Syria, Sudan, Cuba, and North Korea)
*     and that User is not otherwise prohibited
* under the Export Laws from receiving the Software.
* 
* Copyright 2011 Battelle Memorial Institute.  All Rights Reserved.
* Distributed as open-source under the terms of the Educational Community 
* License version 2.0 (ECL 2.0). http://www.opensource.org/licenses/ecl2.php
* 
* For further details, see: http://www.globalchange.umd.edu/models/gcam/
*
*/

/*! 
* \file logbroyden.cpp
* \ingroup objects
* \brief LogBroyden class (Broyden's method solver) source file
* \author Robert Link
*/


#include "util/base/include/definitions.h"
#include <string>
#include <algorithm>
#include <iomanip>
#include <math.h>
#include <xercesc/dom/DOMNode.hpp>
#include <xercesc/dom/DOMNodeList.hpp>

#include "solution/solvers/include/solver_component.h"
#include "solution/solvers/include/logbroyden.hpp"
#include "solution/util/include/calc_counter.h"
#include "marketplace/include/marketplace.h"
#include "containers/include/world.h"
#include "solution/util/include/solution_info_set.h"
#include "solution/util/include/solution_info.h"
#include "solution/util/include/solver_library.h"
#include "util/base/include/util.h"
#include "util/base/include/configuration.h"
#include "util/logger/include/ilogger.h"
#include "util/base/include/xml_helper.h"
#include "solution/util/include/solution_info_filter_factory.h"
#include "solution/util/include/solvable_nr_solution_info_filter.h"

#include "solution/util/include/functor-subs.hpp"
#include "solution/util/include/linesearch.hpp"
#include "solution/util/include/fdjac.hpp" 
#include "solution/util/include/edfun.hpp"
#include "solution/util/include/ublas-helpers.hpp"
#include "util/base/include/fltcmp.hpp"
#include "solution/util/include/jacobian-precondition.hpp"

#if USE_LAPACK
#include <boost/numeric/bindings/traits/ublas_vector.hpp>
#include <boost/numeric/bindings/traits/ublas_matrix.hpp>
#include <boost/numeric/ublas/operation.hpp>
#include <boost/numeric/bindings/lapack/gesvd.hpp>
#include "solution/util/include/svd_invert_solve.hpp"
#else
#include <boost/numeric/ublas/operation.hpp>
#include <boost/numeric/ublas/lu.hpp>
#endif 

#include "util/base/include/timer.h"

using namespace xercesc;

std::string LogBroyden::SOLVER_NAME = "broyden-solver-component";

#if USE_LAPACK
#define UBMATRIX boost::numeric::ublas::matrix<double,boost::numeric::ublas::column_major>
#else
#define UBMATRIX boost::numeric::ublas::matrix<double>
#endif
#define UBVECTOR boost::numeric::ublas::vector<double>


namespace {
  // helper functions for the std::transform algorithm
  inline double SI2lgprice (const SolutionInfo &si) {
    double p = std::max(si.getPrice(), util::getTinyNumber());
    return log( p );
  }
  inline double SI2price (const SolutionInfo &si) {return si.getPrice();}

  // read-only accessor for solutionInfoSet (used to prepare log outputs)
  const SolutionInfoSet *cSolInfo=0;

  // utility function for finding the minimimum and maximum absolute
  // value entries in a vector.
  void locate_vector_minmax(const UBVECTOR &v, double &vmax, double &vmin, int &imax, int &imin)
  {
    vmax = vmin = fabs(v[0]);
    imax = imin = 0;
    for(int i=1; i<v.size(); ++i) {
      double vabs = fabs(v[i]);
      if(vabs < vmin) {
        vmin = vabs;
        imin = i;
      }
      if(vabs > vmax) {
        vmax = vabs;
        imax = i;
      }
    }
  } 
}

int LogBroyden::mLastPer = 0;
int LogBroyden::mPerIter = 0;

bool LogBroyden::XMLParse( const DOMNode* aNode ) {
    // assume we were passed a valid node.
    assert( aNode );
    
    // get the children of the node.
    DOMNodeList* nodeList = aNode->getChildNodes();
    
    // loop through the children
    for ( unsigned int i = 0; i < nodeList->getLength(); ++i ){
        DOMNode* curr = nodeList->item( i );
        std::string nodeName = XMLHelper<std::string>::safeTranscode( curr->getNodeName() );
        
        if( nodeName == "#text" ) {
            continue;
        }
        else if( nodeName == "max-iterations" ) {
            mMaxIter = XMLHelper<unsigned int>::getValue( curr );
        }
        else if( nodeName == "ftol" ) {
            mFTOL = XMLHelper<double>::getValue( curr );
        }
        else if( nodeName == "solution-info-filter" ) {
            mSolutionInfoFilter.reset(
                                      SolutionInfoFilterFactory::createSolutionInfoFilterFromString( XMLHelper<std::string>::getValue( curr ) ) );
        }
        else if(nodeName == "linear-price") {
          mLogPricep = false;
        }
        else if(nodeName == "log-price") {
          mLogPricep = true;    // not strictly necessary, as this is the default.
        }
        else if( SolutionInfoFilterFactory::hasSolutionInfoFilter( nodeName ) ) {
            mSolutionInfoFilter.reset( SolutionInfoFilterFactory::createAndParseSolutionInfoFilter( nodeName, curr ) );
        }
        else {
            ILogger& mainLog = ILogger::getLogger( "main_log" );
            mainLog.setLevel( ILogger::WARNING );
            mainLog << "Unrecognized text string: " << nodeName << " found while parsing "
                    << getXMLName() << "." << std::endl;
        }
    }
    return true;
}


/*! \brief Broyden's method solver. 
 * \details Attempts to solve the selected markets using Broyden's
 * method (see Numerical Recipes sectn. 9.7).  Broyden's method is
 * broadly similar to the Newton-Raphson Method.  At each iteration we
 * solve B . dx = -F to get the solution step for that iteration, we
 * backtrack as necessary to ensure that F . F decreases at each step,
 * and we iterate to convergence.  The difference is that in place of
 * the exact Jacobian, J, we use an approximate Jacobian, B.  At each
 * iteration we update B using Broyden's secant condition: B(i+1) =
 * B( i ) + ((dF( i ) - B( i ) . dx( i )) X dx( i )) / (dx . dx).  Since this
 * update does not require any model evaluations, it is *much* faster
 * than computing finite-difference Jacobians.
 *
 * We still need an initial approximation to the Jacobian.  Whenever
 * possible we'll get that from estimates of derivatives that we've
 * previously squirreled away in the SolutionInfo objects.  When we
 * add a new market to the solution set, we may not have good
 * derivatives for the newcomer.  In that case we'll do finite
 * difference approximations for the new column, and we'll zero the
 * off-diagonal terms of the new row.
 *
 * The solver can run in either log-log mode or linear-linear mode.
 *
 * \author Robert Link 
 * \param solnset An initial set of SolutionInfo objects representing all of the markets we will attempt to solve
 * \param period Model time period
 * \return Status code indicating whether the algorithm was successful or not.
 */
SolverComponent::ReturnCode LogBroyden::solve(SolutionInfoSet &solnset, int period) {
  ReturnCode code = SolverComponent::ORIGINAL_STATE;

    // If all markets are solved, then return with success code.
    if( solnset.isAllSolved() ){
        return code = SolverComponent::SUCCESS;
    }
    
    startMethod();
    if(period != mLastPer) {
        // reset our internal counters
        mPerIter = 0;
        mLastPer = period;
    }
    
    // Update the solution vector for the correct markets to solve.
    // Need to update solvable status before starting solution (Ignore return code)
    solnset.updateSolvable( mSolutionInfoFilter.get() );

    ILogger& solverLog = ILogger::getLogger( "solver_log" );
    solverLog.setLevel( ILogger::NOTICE );
    solverLog << "Beginning Broyden solution for period " << period
              << "Solving " << solnset.getNumSolvable() << "markets.\n";
    if( mLogPricep ) {
      solverLog << "Log price in effect\n";
    }
    else {
      solverLog << "Linear price in effect\n";
    }
    
    ILogger& worstMarketLog = ILogger::getLogger( "worst_market_log" );
    worstMarketLog.setLevel( ILogger::DEBUG );
    ILogger& singleLog = ILogger::getLogger( "single_market_log" );
    singleLog.setLevel( ILogger::DEBUG );
    
    size_t nsolv = solnset.getNumSolvable(); 
    if( nsolv == 0 ){
      solverLog << "No markets were assigned to this solver.  Exiting." << std::endl;
        return SUCCESS;
    }
    
    solverLog << "Initial market state:\nmkt    \tprice   \tsupply  \tdemand\n";
    std::vector<SolutionInfo> solvables = solnset.getSolvableSet();
    for(size_t i=0; i<solvables.size(); ++i) {
        solverLog << std::setw( 8 ) << i << "\t"
                  << std::setw( 8 ) << solvables[i].getPrice() << "\t"
                  << std::setw( 8 ) << solvables[i].getSupply() << "\t"
                  << std::setw( 8 ) << solvables[i].getDemand()
                  << "\t\t" << solvables[i].getName() << "\n"; 
    } 

    Timer& solverTimer = TimerRegistry::getInstance().getTimer( TimerRegistry::SOLVER );
    solverTimer.start();
    
    UBVECTOR x( nsolv ), fx( nsolv );
    int neval = 0;

    // set our initial x from the solutionInfoSet
    std::vector<SolutionInfo> smkts(solnset.getSolvableSet());
    if( mLogPricep ) {
      std::transform(smkts.begin(), smkts.end(), x.begin(), SI2lgprice);
    }
    else {
      std::transform(smkts.begin(), smkts.end(), x.begin(), SI2price);
    }

    
    // This is the closure that will evaluate the ED function
    LogEDFun F(solnset, world, marketplace, period, mLogPricep); 
    // check the assumptions:  narg==nrtn==nsolv
    if(F.narg() != nsolv || F.nrtn() != nsolv) {
      solverLog.setLevel(ILogger::SEVERE);
      solverLog << "size mismatch in logbroyden:  nsolv= " << F.narg()
                << "  nrtn= " << F.nrtn()
                << "  nsolv= " << nsolv
                << std::endl;
      abort();
    }

    // scale the initial guess for use in the solver algorithm
    F.scaleInitInputs( x );
    
    // Call F( x ), store the result in fx
    F(x,fx);

    solverLog.setLevel(ILogger::DEBUG);
    solverLog << "Initial guess:\n" << x << "\nInitial F( x ):\n" << fx << "\n";
    solnset.printMarketInfo("Broyden-initial", calcCounter->getPeriodCount(), singleLog);

    // Precondition the x values to avoid singular columns in the Jacobian
    solverLog.setLevel(ILogger::DEBUG);
    UBMATRIX J(F.narg(), F.nrtn());
    fdjac(F, x, fx, J, true);

    solverLog << ">>>> Main loop jacobian called.\n";
    int pcfail = jacobian_precondition(x, fx, J, F, &solverLog, mLogPricep);

    if( pcfail ) {
      solverLog.setLevel(ILogger::WARNING);
      solverLog << "Unable to find nonsingular initial guess for one or more markets.  bsolve() will probably fail.\n";
      solverLog.setLevel(ILogger::DEBUG);
    }
    else {
      solverLog << "Revised guess:\n" << x << "\nRevised F( x ):\n" << fx << "\n";
    }
    solnset.printMarketInfo("Broyden-preconditioned", calcCounter->getPeriodCount(), singleLog);
    cSolInfo = &solnset;        // make available for log outputs

    // call the solver
    int bstatus = bsolve(F, x, fx, J, neval);
    mPerIter++;                 // increment the iteration count.  This should produce a visible gap in the trace plots.

    solverTimer.stop(); 

    solverLog.setLevel(ILogger::NOTICE);
    solverLog << "Broyden solver:  neval= " << neval << "\nResult:  ";
    if(bstatus == 0) {
        solverLog << "Broyden solution success.\n";
        code = SUCCESS;
    }
    else if(bstatus == -1) {
        code = FAILURE_ITER_MAX_REACHED;
        solverLog << "Broyden solution failed: Iteration max reached.\n";
    }
    else if(bstatus == -3) {
        code = FAILURE_ZERO_GRADIENT;
        solverLog << "Broyden solution failed:  Encountered zero gradient in F*F.\n";
    }
    else if(bstatus == -4) {
        code = FAILURE_POOR_PROGRESS;
        solverLog << "Broyden solution failed:  repeated poor progress.\n";
    }
    else if(bstatus > 0) {
        code = FAILURE_SINGULAR_MATRIX;
        int singrow = bstatus-1; // L-U decomp returns row number as 1..N numbering
        solverLog << "Broyden solution failed:  Encountered singular matrix (row " << singrow << ").\n";
        solverLog << smkts[singrow] << std::endl;
    }
    else {
        code = FAILURE_UNKNOWN;
        solverLog << "Broyden solution failed for unknown reason.\n";
    }
    if(!solnset.isAllSolved()) {
        solverLog << "The following markets were not solved:\n";
        solnset.printUnsolved( solverLog );
    }

    solverLog << std::endl;

    // log some final debugging info
    const SolutionInfo* maxred = solnset.getWorstSolutionInfo();
    addIteration(maxred->getName(), maxred->getRelativeED());
    if( mLogPricep ) {
      worstMarketLog << "###Broyden-end-logPrice:  " << *maxred << std::endl;
    }
    else {
      worstMarketLog << "###Broyden-end-linearPrice:  " << *maxred << std::endl;
    }

    solnset.printMarketInfo("Broyden-end ", calcCounter->getPeriodCount(), singleLog);
    singleLog << std::endl;

    return code;
}

int LogBroyden::bsolve(VecFVec<double,double> &F, UBVECTOR &x, UBVECTOR &fx,
                       UBMATRIX & B, int &neval)
{
#if !USE_LAPACK
  using boost::numeric::ublas::permutation_matrix;
  using boost::numeric::ublas::lu_factorize;
  using boost::numeric::ublas::lu_substitute;
#endif
  using boost::numeric::ublas::axpy_prod;
  using boost::numeric::ublas::inner_prod;
  int nrow = B.size1(), ncol = B.size2();
  int ageB = 0;   // number of iterations since the last reset on B
  // svd decomposition elements (note nrow == ncol)
#if USE_LAPACK
  UBMATRIX Usv(nrow,ncol),VTsv(ncol,ncol);
  UBVECTOR Ssv( ncol );
#else
  permutation_matrix<int> p(F.narg()); // permutation vector for pivoting in L-U decomposition
#endif

  UBMATRIX Btmp(nrow, ncol);
  ILogger &solverLog = ILogger::getLogger("solver_log");
  ILogger& worstMarketLog = ILogger::getLogger( "worst_market_log" );
  worstMarketLog.setLevel( ILogger::DEBUG );
  ILogger& singleLog = ILogger::getLogger( "single_market_log" );
  singleLog.setLevel( ILogger::DEBUG );

  // market ids and solvable flag for just the solvable markets
  std::vector<int> mktids_solv;
  cSolInfo->getMarketIDs(mktids_solv,true);
  std::vector<bool> issolvable_solv(mktids_solv.size(), true);
  // market ids and solvable flag for all markets (solvable and unsolvable)
  std::vector<int> mktids_all;
  cSolInfo->getMarketIDs(mktids_all, false);
  std::vector<bool> issolvable_all(mktids_all.size());
  for(unsigned i=0; i<issolvable_solv.size(); ++i) { // solvable markets are at the beginning; set their flag to true
      issolvable_all[i] = true;
  }
  for(unsigned i=issolvable_solv.size(); i<issolvable_all.size(); ++i) {
      // unsolvable markets at the end of the array; set their flag to false
      issolvable_all[i] = false;
  }
  // working space for variables that will be printed for solvable and unsolvable markets
  UBVECTOR rptvec_all(mktids_all.size());
  
  F(x,fx);

  solverLog.setLevel(ILogger::DEBUG);
  
  neval += 1 + x.size();        // initial function evaluation + jacobian calculations

  const double FTINY = mFTOL*mFTOL;

  UBVECTOR dx(F.narg());
  UBVECTOR xnew(F.narg());
  UBVECTOR gx(F.narg());
  assert(F.nrtn() == F.narg());
  assert(x.size() == F.narg());
  assert(fx.size() == F.nrtn());
  UBVECTOR jdiag(F.narg());
  // do the asserts manually, since we can't turn on normal assertions
  // (GCAM is riddled with asserts that fail under normal
  // circumstances).
  if(F.nrtn() != F.narg() || x.size() != F.narg() || fx.size() != F.nrtn()) {
    solverLog.setLevel(ILogger::SEVERE);
    solverLog << "size mismatch:  nrtn= " << F.nrtn()
              << "  narg= " << F.narg()
              << "  x.size= " << x.size()
              << "  fx.size= " << fx.size()
              << std::endl;
    abort();
  }

  // We create a functor that computes f( x ) = F( x )*F( x ).  It also
  // stores the value of F that it produces as an intermediate.
  FdotF<double,double> fnorm( F );
  double f0 = inner_prod(fx,fx); // already have a value of F on input, so no need to call fnorm yet
  if(f0 < FTINY) {
    // Guard against F=0 since it can cause a NaN in our solver.  This
    // is a more stringent test than our regular convergence test
    return 0;
  }

  bool lsfail = false;        // flag indicating whether we have had a line-search failure
  for(int iter=0; iter<mMaxIter; ++iter) {
    // log some debug info
    
    solverLog << "Broyden iter= " << iter << "\tneval= " << neval << "\n";
    solverLog << "Internal iteration count ( mPerIter )= " << mPerIter << "\n";
    cSolInfo->printMarketInfo("Broyden ", calcCounter->getPeriodCount(), singleLog);
    for(int j=0;j<F.narg();++j) {
      // double bjj= B(j,j);
      // jdiag[j] = bjj;
      jdiag[j] = B(j,j);
    }
    static_cast<LogEDFun&>(F).setSlope(jdiag);
    double jdmax=0.0, jdmin=0.0;
    int jdjmax=0, jdjmin=0;
    locate_vector_minmax(jdiag, jdmax, jdmin, jdjmax, jdjmin);
    solverLog << "diag( B ):\n" << jdiag << "\n";
    solverLog << "maxval= " << jdmax << " jmax= " << jdjmax << "  "
              << "minval= " << jdmin << "  jmin= " << jdjmin << "\n";
    
    axpy_prod(fx,B,gx);         // compute the gradient of F*F (= fx^T * B == B^T * fx)
                                // axpy_prod clears gx on entry, so we don't have to do it.
                                // NB: the order of fx and B in that last call is significant!

    // Check for zero gradient.  This indicates a local minimum in f,
    // from which we are unlikely to escape.  We will need to try
    // again with a different initial guess.  (This should be very
    // uncommon)
    double gmag2 = inner_prod(gx,gx);
    if(gmag2 / (f0+FTINY) < mFTOL) {
      solverLog << "**** ||gx|| = 0.  Returning.\n";
      return -3;
    }

    Btmp = B;                   // save the jacobian approximant
#if USE_LAPACK /* Solve using SVD */
    int ierr = boost::numeric::bindings::lapack::gesvd('O','A','A', // control parameters
                                                       B,           // input matrix
                                                       Ssv,Usv,VTsv); // outputs
    if(ierr>0) {
      // svd failed.  It's not even clear under what circumstances
      // this can happen
      solverLog.setLevel(ILogger::SEVERE);
      solverLog << "****************SVD failed.  This shouldn't happen.  It can't mean anything good.\n";
      return ierr;
    }

    // At this point, U, S, and VT contain the SVD of the original Jacobian
    solverLog.setLevel(ILogger::DEBUG);
    dx = -1.0*fx; 
    int nsing = svdInvertSolve(Usv,Ssv,VTsv,dx, solverLog);

    solverLog << "\nIteration " << iter << "\nf0= " << f0
              << "\tnsing= " << nsing
              << "\nx: " << x << "\nF( x ): " << fx << "\ndx: " << dx << "\n";

#else /* No USE_LAPACK.  Solve using L-U decomposition */
    int itrial = 0;
    /* If the L-U decomposition fails the first time around, we will
       invoke the jacobian preconditioner and try again.  If it fails
       a second time, we bail out */
    do {
      for(size_t i=0; i<p.size(); ++i) {
        p[i] = i;
      }
      int sing = lu_factorize(B,p);
      if(sing>0) {
        int fail=1;
        B = Btmp;           // restore Jacobian
        if(itrial == 0) {
            solverLog << "Salvaging Jacobian.\n";
            fail = jacobian_precondition(x, fx, B, F, &solverLog, mLogPricep);
            f0 = inner_prod(fx,fx);

            // log the diagonal of the new jacobian
            for(int j=0; j<F.narg(); ++j) {
                jdiag[j] = B(j,j); 
            }
            solverLog << "After jacobian salvage.  diag( B )=\n" << jdiag << "\n";

        }
        
        if( fail ) {
            solverLog.setLevel(ILogger::WARNING);
            solverLog << "Singular Jacobian:\n" << B << "\n";
            return sing;
        }
      }
      else {
        // L-U decomp was successful.  Continue with the next phase of the algorithm.
        break;
      }
    } while(++itrial < 2);
    
    // J now holds the L-U decomposition of the Jacobian.  Attempt backsubstitution
    dx = -1.0*fx;
    try {
      lu_substitute(B,p,dx);    // solve dx = J^-1 F
    }
    catch (const boost::numeric::ublas::internal_logic &err) {
      // This error seems to be thrown when the Jacobian is
      // ill-conditioned.  We let it go because often the solver will
      // muddle through to a solution.  If not, then it will
      // eventually stop with a genuinely singular matrix.
    }
    solverLog << "dx: " << dx << "\n"; 
#endif /* USE_LAPACK */

    // log the proposal step
    solverLog << "Proposal step magnitude dxmag= " << sqrt(inner_prod(dx,dx)) << "\n\n";
    reportVec("dxprop", dx, mktids_solv, issolvable_solv);    

    
    // dx now holds the newton step.  Execute the line search along
    // that direction.
    double fnew;
    int lserr = linesearch(fnorm,x,f0,gx,dx, xnew,fnew, neval, &solverLog);

    if(lserr != 0) {
      // line search failed.  There are a couple of things that could
      // be happening here.

      // 1) B is not a descent direction.  If this is the first
      // failure starting from this x value, try a finite difference
      // jacobian
      if(!lsfail) {
        solverLog << "**Failed line search. Evaluating fdjac\n";
        lsfail = true;
        // call fdjac such that it re-calculates the model at x as linesearch will
        // have left off on some other price vector thus we could have bad state
        // data from which we calculate derivatives
        fdjac(F,x,B);
        neval += x.size();
        ageB = 0;  // reset the age on B

        // Log the diagonal of the new jacobian after the failed line search
        for(int j=0; j<F.narg(); ++j) {
            jdiag[j] = B(j,j);
        }
        solverLog << "New Jacobian: diag( B )=\n" << jdiag << "\n";
        static_cast<LogEDFun&>(F).setSlope(jdiag);

        // start the next iteration *without* updating x
        continue;
      }

      // 2) The descent only continues for a very short distance
      // (roughly TOL*x0).  Maybe we're really close to a solution and
      // generating very tiny dx values.  Make a relaxed convergence
      // test and return if we have a "close enough" solution.
      double msf = f0/fx.size();
      if(msf < mFTOL) {
        // basically, we're letting ourselves converge to the sqrt of
        // our intended tolerance.
        return 0;
      }

      // 3) Neither of the above.  The most likely way for this to
      // happen is if we're close to a discontinuity in (probably
      // several components of) the Jacobian.  Using smoother supply
      // and/or demand functions might help here.

      // We're not close enough to the solution, and we don't have a
      // good descent direction.  There are no good options at this point
      // so kick out and hope that the preconditioner can set us straight.
      solverLog << "linesearch failure\n";
      return -4;
    }
    else {
      // reset the line search fail flag
      lsfail = false;
    }

    UBVECTOR xstep(xnew-x);    // step in x eventually taken
    double lambda = fabs(dx[0]) > 0.0 ? xstep[0] / dx[0] : 0.0;
    solverLog << "################Return from linesearch\nfold= " << f0 << "\tfnew= " << fnew
              << "\tlambda= " << lambda << "\n";

    UBVECTOR fxnew(fx.size());
    fnorm.lastF( fxnew );            // get the last value of big-F
    solverLog << "\nxnew: " << xnew << "\nfxnew: " << fxnew << "\n";
    UBVECTOR fxstep(fxnew -fx); // change in F( x ).  We will need this for the secant update

    // log the worst market info
    const SolutionInfo* maxred = cSolInfo->getWorstSolutionInfo();
    addIteration(maxred->getName(), maxred->getRelativeED());
    if( mLogPricep ) {
      worstMarketLog << "Broyden-logPrice:  " << *maxred << "\n";
    }
    else {
      worstMarketLog << "Broyden-linearPrice:  " << *maxred << "\n";
    }

    // test for convergence
    double maxval = fabs(fxnew[0]);
    double imaxval = 0;
    for(size_t i=1; i<fxnew.size(); ++i) {
      double val = fabs(fxnew[i]);
      if(val > maxval) {
        maxval = val;
        imaxval = i;
      }
    }

    solverLog << "Convergence test maxval: " << maxval << "  imaxval= " << imaxval << "\n";
    solverLog << "\tx[i]= " << xnew[imaxval] << "  dx[i]= " << dx[imaxval] << "  xstep[i]= "
              << xstep[imaxval] << "\n";
    if(maxval <= mFTOL) {
      solverLog << "Solution successful.\n";
      x = xnew;
      fx = fxnew;
      return 0;                 // SUCCESS 
    }

#if 0
    // secondary convergence test based on an estimated change in the
    // price vector.  Only test this if we have a "fresh" jacobian
    if(ageB == 0)  {
        maxval = fabs(fxnew[0]) / (util::getSmallNumber() + fabs(Btmp(0,0)));
        imaxval = 0;
        for(size_t i=1; i<fxnew.size(); ++i) {
            double val = fabs(fxnew[i]) / (util::getSmallNumber() + fabs(Btmp(i,i)));
            if(val > maxval) {
                maxval = val;
                imaxval = i;
            }
        }
        solverLog << "Secondary convergence test maxval:  " << maxval << "  imaxval= " << imaxval << "\n";
        if(maxval <= mFTOL) {   // XXX should put an x-tolerance in here
            solverLog << "Solved using secondary criterion.\n";
            x = xnew;
            fx = fxnew;
            return 0; 
        }
    }
#endif
    
    // update B for next iteration
    double fratio_cutoff = 1.0 - 1.0/nrow;
    if(fnew/f0 < fratio_cutoff) { // making adequate progress with the Broyden formula
      double dx2 = inner_prod(xstep,xstep);
      UBVECTOR Bdx(F.nrtn());
      B = Btmp;
      fxstep -= axpy_prod(B, xstep, Bdx);
      fxstep /= dx2;
      B += outer_prod(fxstep, xstep);
      ageB++;                // increment the age of B
    }
    else {
      // Progress using the Broyden formula is anemic.  This usually
      // happens near discontinuities in the Jacobian matrix.  If B is
      // old, try a finite-difference jacobian to get us back on track.
      if(ageB > 0) {
        solverLog << "Insufficient progress with Broyden formula.  Resetting the Jacobian.\n(f0= " << f0 << ", fnew= " << fnew << ")\n";
        // just in case call fdjac such that it re-calculates the model at xnew
        // otherwise we could have bad state data from which we calculate derivatives
        fdjac(F,xnew,B);
        neval += x.size();
        ageB = 0;

        // Log the results of the Jacobian reset
        for(int j=0; j<F.narg(); ++j) {
            jdiag[j] = B(j,j);
        }
            
        solverLog << "New Jacobian:  diag( B )=\n" << jdiag << "\n";
        static_cast<LogEDFun&>(F).setSlope(jdiag);
        
      }
      else {
        // just did a reset, and it didn't help us.  Probably we've
        // got a very ill-behaved value in one of the variables.  Kick
        // it out and see if the bracketing routine can fix it.
        solverLog << "Repeated poor progress in Broyden solver.  Returning.\n";
        return -4;
      }
    }

    // log the data trace before we do the update
    reportVec("x", xnew, mktids_solv, issolvable_solv);
    reportVec("fx", fxnew, mktids_solv, issolvable_solv);
    reportVec("deltax", xnew-x, mktids_solv, issolvable_solv);    // xstep may have been modified above
    reportVec("deltafx", fxnew-fx, mktids_solv, issolvable_solv); // fxstep definitely modified above
    reportVec("diagB", jdiag, mktids_solv, issolvable_solv);
    reportPSD(rptvec_all, mktids_all, issolvable_all);                // report price, supply, and demand.  
    mPerIter++;

    // update x, fx, f0 for next iteration
    f0 = fnew;
    x  = xnew;
    fx = fxnew;

  }

  // if we get here, then we didn't converge in the number of
  // iterations allowed us.  Return an error code
  solverLog << "\n****************Maximum solver iterations exceeded.\nlastx: " << x
            << "\nlastF: " << fx << "\n";
  return -1;
}

/*! \brief Write a vector into the solver data log
 *
 *  \details We write the solver data log in "long" format; i.e., with
 *           one completely-specified data item per line.  This allows
 *           us to simply skip variables that we don't have data for
 *           (vs. filling in NaN values), and we don't have to commit
 *           to a fixed number of variables; e.g., we could have more
 *           variables for price, supply, and demand.
 *
 *           The output columns are:
 *           period, iteration, variable name, market id, solvable (T/F), value
 *
 */
void LogBroyden::reportVec(const std::string &aname, const UBVECTOR &av, const std::vector<int> &amktids,
                           const std::vector<bool> &aissolvable)
{
    ILogger &datalog = ILogger::getLogger("solver-data-log");
    datalog.setLevel(ILogger::DEBUG);
    // Skip preparing the output if it won't be printed
    if(!datalog.wouldPrint(ILogger::DEBUG)) {
        return;
    }

    for(unsigned int i=0; i<av.size(); ++i) {
        datalog << mLastPer <<  " , " << mPerIter
                << ",\"" << aname << "\""
                << ", " << amktids[i]
                << (aissolvable[i] ? ", T" : ", F")
                << ", " << av[i] << "\n";
    }
}

void LogBroyden::reportPSD(UBVECTOR &arptvec, const std::vector<int> &amktids, const std::vector<bool> &aissolvable)
{
    // Skip preparing the log output if we're not going to print it anyhow 
    if(!ILogger::getLogger("solver-data-log").wouldPrint(ILogger::DEBUG)) {
        return;
    }

    if(!cSolInfo) {
        ILogger &solverlog = ILogger::getLogger("solver_log");
        ILogger::WarningLevel olvl = solverlog.setLevel(ILogger::ERROR);
        solverlog << "cSolInfo is not set.  Data log will not include price, supply, or demand." << std::endl;
        solverlog.setLevel( olvl );
        return;
    }

    std::vector<SolutionInfo> solvable = cSolInfo->getSolvableSet();
    std::vector<SolutionInfo> unsolvable = cSolInfo->getUnsolvableSet();

    unsigned int i;
    unsigned int j;
    // log prices
    for(i=0; i<solvable.size(); ++i) {
        arptvec[i] = solvable[i].getPrice();
    }
    for(i=0,j=solvable.size(); i<unsolvable.size(); ++i,++j) {
        arptvec[j] = unsolvable[i].getPrice();
    }
        
    reportVec("price", arptvec, amktids, aissolvable);

    // log supply
    for(i=0; i<solvable.size(); ++i) {
        arptvec[i] = solvable[i].getSupply();
    }
    for(i=0,j=solvable.size(); i<unsolvable.size(); ++i,++j) {
        arptvec[j] = unsolvable[i].getSupply();
    }
    reportVec("supply", arptvec, amktids, aissolvable);

    // log demand
    for(i=0; i<solvable.size(); ++i) {
        arptvec[i] = solvable[i].getDemand();
    }
    for(i=0,j=solvable.size(); i<unsolvable.size(); ++i,++j) {
        arptvec[j] = unsolvable[i].getDemand();
    }
    reportVec("demand", arptvec, amktids, aissolvable);

}

    
