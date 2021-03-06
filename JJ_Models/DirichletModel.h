/*
 * 
 *  Copyright (C) 2010 TU Delft. All rights reserved.
 *  
 *  This class implements a model for dirichlet boundary conditions.
 *
 *  Author:  F.P. van der Meer, F.P.vanderMeer@tudelft.nl
 *  Date:    May 2010
 *
 */


#ifndef DIRICHLET_MODEL_H
#define DIRICHLET_MODEL_H

#include <jem/io/FileReader.h>
#include <jem/util/Properties.h>
#include <jem/io/Writer.h>
#include <jive/Array.h>
#include <jive/model/Model.h>
#include <jive/model/ModelFactory.h>
#include <jive/util/Assignable.h>
#include <jive/util/Constraints.h>
#include <jive/util/DofSpace.h>

using namespace jem;

using jem::io::FileReader;
using jem::util::Properties;
using jem::io::Writer;
using jive::Vector;
using jive::IdxVector;
using jive::model::Model;
using jive::StringVector;
using jive::fem::NodeSet;
using jive::model::Model;
using jive::util::Assignable;
using jive::util::DofSpace;
using jive::util::Constraints;

//-----------------------------------------------------------------------
//   class DirichletModel
//-----------------------------------------------------------------------


class DirichletModel : public Model
{
 public:

  typedef DirichletModel    Self;
  typedef Model             Super;

  enum    Method   { RATE, INCREMENT };   

  static const char*        TYPE_NAME;

  static const char*        MAX_DISP_PROP;
  static const char*        DISP_INCR_PROP;
  static const char*        DISP_RATE_PROP;
  static const char*        INIT_DISP_PROP;
  static const char*        INIT_LOAD_PROP;
  static const char*        NODES_PROP;
  static const char*        DOF_PROP;
  static const char*        FACTORS_PROP;
  static const char*        LOADED_PROP;
  static const char*        LOADS_PROP;

  static const char*        TURN_DISP_PROP;
  static const char*        HOLD_DISP_PROP;

  explicit                  DirichletModel

    ( const String&           name  = "arclen",
      const Ref<Model>&       child = NIL );

  virtual void              configure

    ( const Properties&       props,
      const Properties&       globdat );

  virtual void              getConfig

    ( const Properties&       conf,
      const Properties&       globdat )      const;

  virtual bool              takeAction

    ( const String&           action,
      const Properties&       params,
      const Properties&       globdat );

  static Ref<Model>         makeNew

    ( const String&           name,
      const Properties&       conf,
      const Properties&       props,
      const Properties&       globdat );


 protected:

  virtual                  ~DirichletModel  ();


 private:

  void                      init_

    ( const Properties&       globdat );

  void                      advance_

    ( const Properties&       globdat );

  void                      applyConstraints_

    ( const Properties&       params,
      const Properties&       globdat );

  void                      checkCommit_

    ( const Properties&       params,
      const Properties&       globdat );

  void                      commit_

    ( const Properties&       params,
      const Properties&       globdat );

  void                      reduceStep_

    ( const Properties&       params,
      const Properties&       globdat );

  void                      increaseStep_

    ( const Properties&       params,
      const Properties&       globdat );

  void                      setDT_

    ( const Properties&       params );

 private:

  static const idx_t        U_LOAD_;

  Ref<DofSpace>             dofs_;
  Ref<Constraints>          cons_;
  Assignable<NodeSet>       nodes_;

  idx_t                     ngroups_;
  IdxVector                 idofs_;

  double                    dispScale0_;
  double                    dispScale_;

  // input either increment size or rate

  double                    dispIncr_;
  double                    dispRate_;
  Method                    method_;

  /*
   * the following members are input for specifying boundary conditions
   *
   */

  StringVector              nodeGroups_;
  StringVector              dofTypes_;
  Vector                    factors_;

  /* the following members are constant input variables 
   *
   * dispIncr:    initial displacement increment
   * maxDispVal:  maximum (absolute) displacement value (in disp.control)
   *
   */

  double                    dispIncr0_; 
  double                    maxDispVal_;
  double                    initDisp_; 

  Properties                lbcProps_;
  
  // Added by Erik
  double                    turnDispVal_;
  double                    holdDispVal_;
  bool                      turnBool_;
  bool                      unloadBool_;

};

#endif
