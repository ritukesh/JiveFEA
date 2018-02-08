/*
 * 
 *  Copyright (C) 2013 TU Delft. All rights reserved.
 *  
 *  This class implements a strategy for adaptive stepping 
 *  with a SolverModule for tracing equilibrium paths without
 *  snapback behavior.
 *
 *  Author: F.P. van der Meer
 *  Date: May 2013
 *
 */
 
/* MODFIED IMPLEMENTATION:
 * 
 *  Copyright (C) 2016 TU Delft. All rights reserved.
 *  
 *  The original model had a default implementation for the Solver. The modified
 *  implementation allows a solver to be given as input. This means the method
 *  can be used for other solvers than the NonLin, even for implicit dynamics. 
 *
 *  Author: E.C. Simons
 *  Date  : 2016
 *
 */
#include <jem/base/limits.h>
#include <jem/base/System.h>
#include <jem/base/Exception.h>
#include <jem/base/IllegalOperationException.h>
#include <jem/io/FileWriter.h>
#include <jem/util/Properties.h>
#include <jem/util/StringUtils.h>
#include <jem/numeric/algebra/utilities.h>
#include <jive/util/Globdat.h>
#include <jive/util/utilities.h>
#include <jive/util/DofSpace.h>
#include <jive/implict/SolverInfo.h>
#include <jive/implict/SolverModule.h>
#include <jive/app/ModuleFactory.h>
#include <jive/model/Actions.h>

#include "AdaptiveStepGeneralModule.h"
#include "SolverNames.h"


using jem::IllegalOperationException;
using jem::System;
using jem::max;
using jem::maxOf;
using jem::newInstance;
using jem::io::endl;
using jem::io::FileWriter;
using jem::util::StringUtils;
using jive::Vector;
using jive::implict::SolverInfo;
using jive::util::Globdat;
using jive::util::joinNames;
using jive::util::DofSpace;
using jive::implict::newSolverModule;


//=======================================================================
//   class AdaptiveStepGeneralModule
//=======================================================================

//-----------------------------------------------------------------------
//   static data
//-----------------------------------------------------------------------

const char* AdaptiveStepGeneralModule::TYPE_NAME       = "AdaptiveStepGeneral";
const char* AdaptiveStepGeneralModule::SOLVER          = "solver";
const char* AdaptiveStepGeneralModule::WRITE_STAT_PROP = "writeStats";
const char* AdaptiveStepGeneralModule::OPT_ITER_PROP   = "optIter";
const char* AdaptiveStepGeneralModule::MIN_INCR_PROP   = "minIncr";
const char* AdaptiveStepGeneralModule::MAX_INCR_PROP   = "maxIncr";
const char* AdaptiveStepGeneralModule::START_INCR_PROP = "startIncr";
const char* AdaptiveStepGeneralModule::REDUCTION_PROP  = "reduction";
const char* AdaptiveStepGeneralModule::STRICT_PROP     = "strict";

//-----------------------------------------------------------------------
//   constructor & destructor
//-----------------------------------------------------------------------

AdaptiveStepGeneralModule::AdaptiveStepGeneralModule 

  ( const String&  name,
    Ref<SolverModule>  solver ) :

      Super    ( name   ),
      solver_  ( solver )

{
  istep_          = 0;
  istep0_         = 0;
  nCancels_       = 0;
  nContinues_     = 0;
  maxNIter_       = 0;

  nRunTotal_      = 0;
  nCancTotal_     = 0;
  nContTotal_     = 0;
  nIterTotal_     = 0;
  nCancCont_      = 0;
  nCommTotal_     = 0;

  startIncr_      = 1.;
  minIncr_        = 1.e-5;
  maxIncr_        = 1.;
  timeMax_        = 1.e99;
  timeA_          = 1.e99;
  timeB_          = 1.e99;
  reduction_      = 0.45;
  optIter_        = 5;
  increment_      = 0.;

  maxIncrTried_   = 0.;
  writeStats_     = false;
  triedSmall_     = false;
  strict_         = false;
}

AdaptiveStepGeneralModule::~AdaptiveStepGeneralModule ()
{}


//-----------------------------------------------------------------------
//   init
//-----------------------------------------------------------------------


Module::Status AdaptiveStepGeneralModule::init

  ( const Properties&  conf,
    const Properties&  props,
    const Properties&  globdat )

{
  model_ = Model::get ( globdat, getContext() );
  istep_ = istep0_ = 0;

  globdat.set ( Globdat::TIME_STEP, istep_ );
  globdat.set ( "var.accepted", false );

  solver_->init ( conf, props, globdat );

  return OK;
}


//-----------------------------------------------------------------------
//   run
//-----------------------------------------------------------------------


Module::Status AdaptiveStepGeneralModule::run ( const Properties& globdat )

{
  using jem::Exception;

  Properties  info      = SolverInfo::get ( globdat );
  Properties  params;

  bool        accept    = true;
  bool        discard   = false;
  bool        converged = false;
  String      terminate;

  nRunTotal_  ++;

  info.clear ();

  setStepSize_( globdat );

  solver_->advance ( globdat );

  // Store maximum values of increment tried in this time step

  if ( nContinues_ == 0 )
  {
    maxIncrTried_ = max ( maxIncrTried_, increment_ );
  }
  
  // Advance the time/load step number

  istep_ = istep0_ + 1;

  globdat.set ( Globdat::TIME_STEP, istep_ );

  // try to find a solution

  try
  {
    solver_->solve ( info, globdat );

    converged = true;
  }
  catch ( const jem::Exception ex )
  {
    System::out() << "AdaptiveStepGeneralModule caught a solver exception: \n" 
                  << "   " << ex.where() << "\n"
                  << "   " << ex.what() << "\n\n";
                  
    if ( ex.what() == "invalid matrix element(s): NaN" )
    {
      //return EXIT;
    }

    discard    = true;
  }

  // get # of iterations, NB: not set when error thrown by solver
  
  idx_t          iterCount = 0; 
  info.find   (  iterCount , SolverInfo::ITER_COUNT );

  maxNIter_    = max ( maxNIter_, iterCount );
  nIterTotal_ += iterCount;

  if ( nContinues_ > 0 ) nIterCont_ += iterCount;

  if ( converged )
  {
    // Ask the model whether to accept converged solution
    // (Model can ask for cancel via DISCARD)

    model_->takeAction ( SolverNames::CHECK_COMMIT, params, globdat );

    params.find ( discard, SolverNames::DISCARD );

    params.find ( accept,   SolverNames::ACCEPT    );
  }

  // set flag in globdat for output indicating solution quality

  globdat.set ( "var.accepted", ( accept && ! discard ) );

  if ( ! discard )
  {
    // take measures based on result of CHECK_COMMIT

    if ( accept )
    {
      // The final solution for this time step has been obtained

      commit_ ( globdat );

      if ( params.find ( terminate, SolverNames::TERMINATE ) ) 
      {
        if ( terminate == "sure" )
        {
          System::out() << "Some model gave the terminate signal during "
            << "checkCommit,\nAdaptiveStepGeneralModule terminates run.\n"
            << "NB: output modules are not called for this time step\n";
          return EXIT;
        }
      }
    }
    else
    {
      // We have an equilibrium solution but it is not accepted:
      // continue with this time step

      continue_ ( params, globdat );
    }
  }
  else
  {
    // 1. discard solution

    cancel_ ( globdat );

    // 2. find proper strategy to try again
   
    // try to reduce the step size

    if ( reduceStep_ ( globdat ) )
    {
      System::out() << "Returning to the same load step "
                    << "with reduced step\n";
    }

    // try to increase the step size

    else if ( increaseStep_ ( globdat ) )
    {
      System::out() << "Returning to the same load step "
                    << "with increased step\n";
    }

    // give it up

    else
    {
      System::out() << "\n*** AdaptiveStepGeneralModule is out of inspiration,\n"
                    << "    can't find a winning strategy for this step."
                    << " Sorry.\n\n";

      return EXIT;
    }
  }
  
  
  // check if the maximum simulated time has been reached
  double t;
  if ( globdat.find ( t, Globdat::TIME ) )
  {
    if ( t > timeMax_ )
    { 
      System::out() << "\n=======================================================\n";
      System::out() << "AdaptiveStepGeneralModule reached maximum simulated time.\nSimulation will now be terminated.\n";
      System::out() << "=======================================================\n\n";
      return EXIT;
    }
  }
  
  return OK;
}


//-----------------------------------------------------------------------
//   shutdown
//-----------------------------------------------------------------------


void AdaptiveStepGeneralModule::shutdown ( const Properties& globdat )
{
  solver_->shutdown ( globdat );

  System::out() << "AdaptiveStepGeneralModule statistics ..." 
    << "\n... total # of runs: " << nRunTotal_ 
    << "\n... # of commits:    " << nCommTotal_
    << "\n... # of continues:  " << nContTotal_
    << "\n-------  ( of which     " << nCancCont_ << " were canceled.)"
    << "\n... # of cancels:    " << nCancTotal_
    << "\n..."
    << "\n... total # of iterations: " << nIterTotal_
    << "\n-------  ( of which " << nIterCont_ << " after continue.)"
    << endl;
}


//-----------------------------------------------------------------------
//   configure
//-----------------------------------------------------------------------


void AdaptiveStepGeneralModule::configure

  ( const Properties&  props,
    const Properties&  globdat )

{
  Properties myProps = props.findProps ( myName_ );

  myProps.find ( writeStats_ , WRITE_STAT_PROP );
  
  solver_->configure ( props, globdat );

  if ( writeStats_ )
  {
    cpuTimer_.start ();

    statOut_  = newInstance<PrintWriter>(
                newInstance<FileWriter> ( "flex.stats" ) );
    *statOut_ << "   nRun  nComm  nCanc  nCont  nCaCo  nIter  CPU" << endl;
  }

  myProps.find ( reduction_, REDUCTION_PROP  );
  myProps.find ( startIncr_, START_INCR_PROP );

  minIncr_ = startIncr_ * 1.e-3;
  maxIncr_ = startIncr_;

  increment_ = startIncr_;
   
  // set time step for dynamic solver
  double tmp; 
  if ( myProps.find ( tmp, "solver.deltaTime" ) )
  {
    System::warn() << "input value for solver.deltaTime will be overwritten by "
      "AdaptiveStepModule. Value of startIncr will be used" << endl;
  }
  props.set ( joinNames ( myName_ , ".solver.deltaTime" ) , increment_ ); 
  
  // 
  myProps.find ( minIncr_,   MIN_INCR_PROP   );
  myProps.find ( maxIncr_,   MAX_INCR_PROP   );
  myProps.find ( timeMax_,  "timeMax"        );
  myProps.find ( timeA_  ,  "timeA"          );
  myProps.find ( timeB_  ,  "timeB"          );
  myProps.find ( optIter_,   OPT_ITER_PROP   );
  
  myProps.find ( strict_ ,   STRICT_PROP      );
}


//-----------------------------------------------------------------------
//   getConfig
//-----------------------------------------------------------------------


void AdaptiveStepGeneralModule::getConfig

  ( const Properties&  conf,
    const Properties&  globdat ) const

{
  Properties  myConf  = conf.makeProps ( myName_ );

  myConf.set ( "type", TYPE_NAME );

  myConf.set ( WRITE_STAT_PROP, writeStats_ );
  myConf.set ( REDUCTION_PROP , reduction_  );
  myConf.set ( START_INCR_PROP, startIncr_  );
  myConf.set ( MAX_INCR_PROP  , maxIncr_    );
  myConf.set ( MIN_INCR_PROP  , minIncr_    );
  myConf.set ( "timeMax"      , timeMax_    );
  myConf.set ( "timeA"        , timeA_      );
  myConf.set ( "timeB"        , timeB_      );
  myConf.set ( OPT_ITER_PROP  , optIter_    );
  myConf.set ( STRICT_PROP    , strict_     );

  solver_->getConfig ( conf, globdat );
}

//-----------------------------------------------------------------------
//   commit_
//-----------------------------------------------------------------------

void AdaptiveStepGeneralModule::commit_

  ( const Properties&       globdat )

{
  // store this solution and proceed with next time step
  // model->takeAction(COMMIT) is called by solver!

  solver_->commit ( globdat );

  ++nCommTotal_;

  // do something to time step size comaring nIter4Adapt with optIter

  double fac  = ( maxNIter_ - optIter_ ) / 4.0; 
  if ( ( maxNIter_ > optIter_ && increment_ > minIncr_ ) || 
       ( maxNIter_ < optIter_ && increment_ < maxIncr_ ) )
  {
    increment_ *= ::pow ( 0.5, fac );
  }
  
  // set step size for particular range of times
  double t;
  globdat.find ( t, Globdat::TIME );
  if ( (t > timeA_) && (t < timeB_) )
  {
    increment_ = minIncr_;
  }
  
  
  // ECS 2016-12-08: Be strict on increment limits!
  if ( strict_ )
  {
    ( increment_ > maxIncr_ ) ? increment_ = maxIncr_: 0;
    ( increment_ < minIncr_ ) ? increment_ = minIncr_: 0;
  }

  // reset variables

  istep0_ = istep_;

  maxNIter_       = 0;
  nCancels_       = 0;
  nContinues_     = 0;
  maxIncrTried_   = 0.;
  triedSmall_     = false;

  // write statistics (optional)

  if ( writeStats_ )
  {
    statOut_->nformat.setFractionDigits ( 4 );
    statOut_->nformat.setIntegerWidth   ( 6 );
    statOut_->nformat.setScientific     ( true );

    *statOut_ <<         nRunTotal_  << ' '  << nCommTotal_
              << ' '  << nCancTotal_ << ' '  << nContTotal_
              << ' '  << nCancCont_  << ' '  << nIterTotal_ 
              << "  " << cpuTimer_.toDouble() 
              << endl;

    statOut_->flush();
  }
}

//-----------------------------------------------------------------------
//   continue_
//-----------------------------------------------------------------------

void AdaptiveStepGeneralModule::continue_

  ( const Properties&       params,
    const Properties&       globdat )

{
  // the solver is not canceled, because the current solution
  // (converged but not accepted) is a good starting point
  
  System::out() << "Continuing with this load step\n\n";

  istep_ = istep0_;

  globdat.set ( Globdat::TIME_STEP, istep_ );

  ++nContinues_;
  ++nContTotal_;

  params.set ( SolverNames::N_CONTINUES, nContinues_ );

  model_->takeAction ( SolverNames::CONTINUE, params, globdat );
}

//-----------------------------------------------------------------------
//   cancel_
//-----------------------------------------------------------------------

void AdaptiveStepGeneralModule::cancel_

  ( const Properties&       globdat )

{
  // discard solution, go back to the beginning of the time step
  // model->takeAction(CANCEL) is called by solver!

  ++nCancels_;
  ++nCancTotal_;
  
  nCancCont_  += nContinues_;
  nContinues_  = 0;
  maxNIter_    = 0;

  solver_->cancel ( globdat );

  istep_ = istep0_;

  globdat.set ( Globdat::TIME_STEP, istep_ );
}

//-----------------------------------------------------------------------
//   reduceStep_
//-----------------------------------------------------------------------

bool AdaptiveStepGeneralModule::reduceStep_

  ( const Properties&       globdat )

{
  if ( !triedSmall_ && increment_ > minIncr_ )
  {
    increment_ *= reduction_;

    return true;
  }
  else
  {
    triedSmall_ = true;

    return false;
  }
}

//-----------------------------------------------------------------------
//   increaseStep_
//-----------------------------------------------------------------------

bool AdaptiveStepGeneralModule::increaseStep_

  ( const Properties&       globdat )

{
  if ( maxIncrTried_ > maxIncr_ )
  {
    return false;
  }
  else
  {
    maxIncrTried_ = increment_ = maxIncrTried_ / reduction_;

    return true;
  }
}

//-----------------------------------------------------------------------
//   setStepSize()
//-----------------------------------------------------------------------

void AdaptiveStepGeneralModule::setStepSize_

  ( const Properties& globdat ) const

{
  using jive::model::ActionParams;
  using jive::model::Actions;
  
  Properties params;

  params.set ( SolverNames::STEP_SIZE  , increment_ );
  params.set ( SolverNames::STEP_SIZE_0, startIncr_ );
  globdat.set ( "ERIK", increment_ );
  
  //double percentage = increment_ / startIncr_ * 100.;
  //System::out() << "Setting step size to " << increment_ << " ("
  // << percentage << "\% of initial step size)" << endl;

  // set step size in models
  model_->takeAction ( SolverNames::SET_STEP_SIZE, params, globdat );
  
  // set step size in solverModule
  Properties props;
  props.set ( joinNames ( myName_ , ".solver.deltaTime" ) , increment_ );	
  solver_->configure ( props, globdat );
  
  
  // Erik 2016-10-14: There is a bug in the program when used in combination
  // with the AdaptiveStepGeneralModule. Both in the current Model and the other 
  // Module can a step size be defined. The actual initial step size used by 
  // the program is the one defined in the DirichletModel. The step size from 
  // the AdaptiveStepGeneral is merely used to scale the step size by the Model...
}

//-----------------------------------------------------------------------
//   makeNew
//-----------------------------------------------------------------------

Ref<Module>  AdaptiveStepGeneralModule::makeNew

  ( const String&           name,
    const Properties&       conf,
    const Properties&       props,
    const Properties&       globdat )

{
  Properties  myConf  = conf .makeProps ( name );
  Properties  myProps = props.findProps ( name );

  Ref<SolverModule> solver = newSolverModule 
    ( joinNames ( name, SOLVER ), conf, props, globdat  );

  return newInstance<Self> ( name, solver );
}


//=======================================================================
//   related functions
//=======================================================================

//-----------------------------------------------------------------------
//   declareAdaptiveStepGeneralModule
//-----------------------------------------------------------------------

void declareAdaptiveStepGeneralModule ()
{
  using jive::app::ModuleFactory;

  ModuleFactory::declare ( AdaptiveStepGeneralModule::TYPE_NAME,
                         & AdaptiveStepGeneralModule::makeNew );
}

