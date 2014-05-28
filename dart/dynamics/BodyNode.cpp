/*
 * Copyright (c) 2011-2014, Georgia Tech Research Corporation
 * All rights reserved.
 *
 * Author(s): Sehoon Ha <sehoon.ha@gmail.com>
 *            Jeongseok Lee <jslee02@gmail.com>
 *
 * Georgia Tech Graphics Lab and Humanoid Robotics Lab
 *
 * Directed by Prof. C. Karen Liu and Prof. Mike Stilman
 * <karenliu@cc.gatech.edu> <mstilman@cc.gatech.edu>
 *
 * This file is provided under the following "BSD-style" License:
 *   Redistribution and use in source and binary forms, with or
 *   without modification, are permitted provided that the following
 *   conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *   CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *   MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 *   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 *   USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 *   AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *   POSSIBILITY OF SUCH DAMAGE.
 */

#include "dart/dynamics/BodyNode.h"

#include <algorithm>
#include <vector>
#include <string>

#include "dart/common/Console.h"
#include "dart/math/Helpers.h"
#include "dart/optimizer/Problem.h"
#include "dart/optimizer/Function.h"
#include "dart/optimizer/nlopt/NloptSolver.h"
#include "dart/renderer/RenderInterface.h"
#include "dart/dynamics/Joint.h"
#include "dart/dynamics/Shape.h"
#include "dart/dynamics/Skeleton.h"
#include "dart/dynamics/Marker.h"

#define DART_DEFAULT_FRICTION_COEFF 1.0
#define DART_DEFAULT_RESTITUTION_COEFF 0.0

namespace dart {
namespace dynamics {

int BodyNode::msBodyNodeCount = 0;

//==============================================================================
BodyNode::BodyNode(const std::string& _name)
  : mSkelIndex(-1),
    mName(_name),
    mIsCollidable(true),
    mIsColliding(false),
    mSkeleton(NULL),
    mParentJoint(NULL),
    mParentBodyNode(NULL),
    mChildBodyNodes(std::vector<BodyNode*>(0)),
    mGravityMode(true),
    mCenterOfMass(Eigen::Vector3d::Zero()),
    mMass(1.0),
    mIxx(1.0),
    mIyy(1.0),
    mIzz(1.0),
    mIxy(0.0),
    mIxz(0.0),
    mIyz(0.0),
    mFrictionCoeff(DART_DEFAULT_FRICTION_COEFF),
    mRestitutionCoeff(DART_DEFAULT_RESTITUTION_COEFF),
    mI(Eigen::Matrix6d::Identity()),
    mW(Eigen::Isometry3d::Identity()),
    mV(Eigen::Vector6d::Zero()),
    mPartialAcceleration(Eigen::Vector6d::Zero()),
    mA(Eigen::Vector6d::Zero()),
    mF(Eigen::Vector6d::Zero()),
    mFext(Eigen::Vector6d::Zero()),
    mFgravity(Eigen::Vector6d::Zero()),
    mArtInertia(Eigen::Matrix6d::Identity()),
    mArtInertiaImplicit(Eigen::Matrix6d::Identity()),
    mBiasForce(Eigen::Vector6d::Zero()),
    mID(BodyNode::msBodyNodeCount++),
    mIsBodyJacobianDirty(true),
    mIsBodyJacobianTimeDerivDirty(true),
    mDelV(Eigen::Vector6d::Zero()),
    mBiasImpulse(Eigen::Vector6d::Zero()),
    mConstraintImpulse(Eigen::Vector6d::Zero()),
    mImpF(Eigen::Vector6d::Zero())
{
}

BodyNode::~BodyNode() {
  for (std::vector<Shape*>::const_iterator it = mVizShapes.begin();
       it != mVizShapes.end(); ++it)
    delete (*it);

  for (std::vector<Shape*>::const_iterator itColShape = mColShapes.begin();
       itColShape != mColShapes.end(); ++itColShape)
    if (mVizShapes.end() == std::find(mVizShapes.begin(), mVizShapes.end(),
                                      *itColShape))
      delete (*itColShape);

  for (std::vector<Marker*>::const_iterator it = mMarkers.begin();
       it != mMarkers.end(); ++it)
    delete (*it);

  delete mParentJoint;
}

void BodyNode::setName(const std::string& _name) {
  mName = _name;
}

const std::string& BodyNode::getName() const {
  return mName;
}

void BodyNode::setGravityMode(bool _gravityMode) {
  mGravityMode = _gravityMode;
}

bool BodyNode::getGravityMode() const {
  return mGravityMode;
}

bool BodyNode::isCollidable() const {
  return mIsCollidable;
}

void BodyNode::setCollidable(bool _isCollidable) {
  mIsCollidable = _isCollidable;
}

void BodyNode::setMass(double _mass) {
  assert(_mass >= 0.0 && "Negative mass is not allowable.");
  mMass = _mass;
  _updateGeralizedInertia();
}

double BodyNode::getMass() const {
  return mMass;
}

//==============================================================================
void BodyNode::setFrictionCoeff(double _coeff)
{
  assert(0.0 <= _coeff
         && "Coefficient of friction should be non-negative value.");
  mFrictionCoeff = _coeff;
}

//==============================================================================
double BodyNode::getFrictionCoeff() const
{
  return mFrictionCoeff;
}

//==============================================================================
void BodyNode::setRestitutionCoeff(double _coeff)
{
  assert(0.0 <= _coeff && _coeff <= 1.0
         && "Coefficient of restitution should be in range of [0, 1].");
  mRestitutionCoeff = _coeff;
}

//==============================================================================
double BodyNode::getRestitutionCoeff() const
{
  return mRestitutionCoeff;
}

BodyNode* BodyNode::getParentBodyNode() const {
  return mParentBodyNode;
}

void BodyNode::addChildBodyNode(BodyNode* _body) {
  assert(_body != NULL);
  mChildBodyNodes.push_back(_body);
  _body->mParentBodyNode = this;
}

BodyNode* BodyNode::getChildBodyNode(int _idx) const {
  assert(0 <= _idx && _idx < mChildBodyNodes.size());
  return mChildBodyNodes[_idx];
}

int BodyNode::getNumChildBodyNodes() const {
  return mChildBodyNodes.size();
}

void BodyNode::addMarker(Marker* _marker) {
  mMarkers.push_back(_marker);
}

int BodyNode::getNumMarkers() const {
  return mMarkers.size();
}

Marker* BodyNode::getMarker(int _idx) const {
  return mMarkers[_idx];
}

bool BodyNode::dependsOn(int _genCoordIndex) const {
  return std::binary_search(mDependentGenCoordIndices.begin(),
                            mDependentGenCoordIndices.end(),
                            _genCoordIndex);
}

int BodyNode::getNumDependentGenCoords() const {
  return mDependentGenCoordIndices.size();
}

int BodyNode::getDependentGenCoordIndex(int _arrayIndex) const {
  assert(0 <= _arrayIndex && _arrayIndex < mDependentGenCoordIndices.size());
  return mDependentGenCoordIndices[_arrayIndex];
}

void BodyNode::fitWorldTransform(const Eigen::Isometry3d& _target,
                                 InverseKinematicsPolicy _policy,
                                 bool _jointLimit)
{
  if (_policy == IKP_PARENT_JOINT)
    fitWorldTransformParentJointImpl(_target, _jointLimit);
  else if (_policy == IKP_ANCESTOR_JOINTS)
    fitWorldTransformAncestorJointsImpl(_target, _jointLimit);
  else if (_policy == IKP_ALL_JOINTS)
    fitWorldTransformAllJointsImpl(_target, _jointLimit);
}

void BodyNode::fitWorldLinearVel(const Eigen::Vector3d& _targetLinVel,
                                 BodyNode::InverseKinematicsPolicy /*_policy*/,
                                 bool _jointVelLimit)
{
  // TODO: Only IKP_PARENT_JOINT policy is supported now.

  Joint* parentJoint = getParentJoint();
  size_t dof = parentJoint->getNumGenCoords();

  if (dof == 0)
    return;

  optimizer::Problem prob(dof);

  // Use the current joint configuration as initial guess
  prob.setInitialGuess(parentJoint->getGenVels());

  // Objective function
  VelocityObjFunc obj(this, _targetLinVel, VelocityObjFunc::VT_LINEAR, mSkeleton);
  prob.setObjective(&obj);

  // Joint limit
  if (_jointVelLimit)
  {
    prob.setLowerBounds(parentJoint->getGenVelsMin());
    prob.setUpperBounds(parentJoint->getGenVelsMax());
  }

  // Solve with gradient-free local minima algorithm
  optimizer::NloptSolver solver(&prob, NLOPT_LN_BOBYQA);
  solver.solve();

  // Set optimal configuration of the parent joint
  Eigen::VectorXd jointDQ = prob.getOptimalSolution();
  parentJoint->setGenVels(jointDQ, true, true);
}

void BodyNode::fitWorldAngularVel(const Eigen::Vector3d& _targetAngVel,
                                  BodyNode::InverseKinematicsPolicy /*_policy*/,
                                  bool _jointVelLimit)
{
  // TODO: Only IKP_PARENT_JOINT policy is supported now.

  Joint* parentJoint = getParentJoint();
  size_t dof = parentJoint->getNumGenCoords();

  if (dof == 0)
    return;

  optimizer::Problem prob(dof);

  // Use the current joint configuration as initial guess
  prob.setInitialGuess(parentJoint->getGenVels());

  // Objective function
  VelocityObjFunc obj(this, _targetAngVel, VelocityObjFunc::VT_ANGULAR, mSkeleton);
  prob.setObjective(&obj);

  // Joint limit
  if (_jointVelLimit)
  {
    prob.setLowerBounds(parentJoint->getGenVelsMin());
    prob.setUpperBounds(parentJoint->getGenVelsMax());
  }

  // Solve with gradient-free local minima algorithm
  optimizer::NloptSolver solver(&prob, NLOPT_LN_BOBYQA);
  solver.solve();

  // Set optimal configuration of the parent joint
  Eigen::VectorXd jointDQ = prob.getOptimalSolution();
  parentJoint->setGenVels(jointDQ, true, true);
}

const Eigen::Isometry3d& BodyNode::getWorldTransform() const {
  return mW;
}

const Eigen::Vector6d& BodyNode::getBodyVelocity() const {
  return mV;
}

Eigen::Vector6d BodyNode::getWorldVelocity(
    const Eigen::Vector3d& _offset, bool _isLocal) const {
  Eigen::Isometry3d T = mW;
  if (_isLocal)
    T.translation() = mW.linear() * -_offset;
  else
    T.translation() = -_offset;
  return math::AdT(T, mV);
}

//==============================================================================
const Eigen::Vector6d& BodyNode::getBodyAcceleration() const
{
  return mA;
}

Eigen::Vector6d BodyNode::getWorldAcceleration(
    const Eigen::Vector3d& _offset, bool _isOffsetLocal) const {
  Eigen::Isometry3d T = mW;
  if (_isOffsetLocal)
    T.translation() = mW.linear() * -_offset;
  else
    T.translation() = -_offset;

  Eigen::Vector6d dV = mA;
  dV.tail<3>() += mV.head<3>().cross(mV.tail<3>());

  return math::AdT(T, dV);
}

//==============================================================================
const math::Jacobian& BodyNode::getBodyJacobian()
{
  if (mIsBodyJacobianDirty)
    _updateBodyJacobian();

  return mBodyJacobian;
}

math::Jacobian BodyNode::getWorldJacobian(
    const Eigen::Vector3d& _offset, bool _isOffsetLocal) {
  Eigen::Isometry3d T = mW;
  if (_isOffsetLocal)
    T.translation() = mW.linear() * -_offset;
  else
    T.translation() = -_offset;
  return math::AdTJac(T, getBodyJacobian());
}

const math::Jacobian& BodyNode::getBodyJacobianTimeDeriv() {
  if (mIsBodyJacobianTimeDerivDirty)
    _updateBodyJacobianTimeDeriv();
  return mBodyJacobianTimeDeriv;
}

math::Jacobian BodyNode::getWorldJacobianTimeDeriv(
    const Eigen::Vector3d& _offset, bool _isOffsetLocal) {
  Eigen::Isometry3d T = mW;
  if (_isOffsetLocal)
    T.translation() = mW.linear() * -_offset;
  else
    T.translation() = -_offset;

  math::Jacobian bodyJacobianTimeDeriv = getBodyJacobianTimeDeriv();
  for (int i = 0; i < mBodyJacobianTimeDeriv.cols(); ++i)
  {
    bodyJacobianTimeDeriv.col(i).tail<3>()
        += mV.head<3>().cross(mBodyJacobian.col(i).tail<3>());
  }

  return math::AdTJac(T, bodyJacobianTimeDeriv);
}

//==============================================================================
const Eigen::Vector6d& BodyNode::getBodyVelocityChange() const
{
  return mDelV;
}

void BodyNode::setColliding(bool _isColliding) {
  mIsColliding = _isColliding;
}

bool BodyNode::isColliding() {
  return mIsColliding;
}

void BodyNode::init(Skeleton* _skeleton, int _skeletonIndex)
{
  assert(_skeleton);

  mSkeleton = _skeleton;
  mSkelIndex = _skeletonIndex;
  mParentJoint->init(_skeleton, _skeletonIndex);

  //--------------------------------------------------------------------------
  // Fill the list of generalized coordinates this node depends on, and sort
  // it.
  //--------------------------------------------------------------------------
  if (mParentBodyNode)
    mDependentGenCoordIndices = mParentBodyNode->mDependentGenCoordIndices;

  else
    mDependentGenCoordIndices.clear();
  for (int i = 0; i < mParentJoint->getNumGenCoords(); i++)
    mDependentGenCoordIndices.push_back(
          mParentJoint->getGenCoord(i)->getSkeletonIndex());
  std::sort(mDependentGenCoordIndices.begin(), mDependentGenCoordIndices.end());

#ifndef NDEBUG
  // Check whether there is duplicated indices.
  int nDepGenCoordIndices = mDependentGenCoordIndices.size();
  for (int i = 0; i < nDepGenCoordIndices - 1; i++)
  {
    for (int j = i + 1; j < nDepGenCoordIndices; j++)
    {
      assert(mDependentGenCoordIndices[i] !=
          mDependentGenCoordIndices[j] &&
          "Duplicated index is found in mDependentGenCoordIndices.");
    }
  }
#endif

  //--------------------------------------------------------------------------
  // Set dimensions of dynamics matrices and vectors.
  //--------------------------------------------------------------------------
  int numDepGenCoords = getNumDependentGenCoords();
  mBodyJacobian.setZero(6, numDepGenCoords);
  mBodyJacobianTimeDeriv.setZero(6, numDepGenCoords);

  //--------------------------------------------------------------------------
  // Set dimensions of cache data for recursive algorithms
  //--------------------------------------------------------------------------
  int dof = mParentJoint->getNumGenCoords();
//  mAI_S.setZero(6, dof);
//  mPsi.setZero(dof, dof);
//  mImplicitPsi.setZero(dof, dof);
//  mAlpha.setZero(dof);

  // TODO(JS): Temporary code
//  mParentJoint->mChildBodyNode = this;
//  if (mParentBodyNode)
//    mParentJoint->mParentBodyNode = this;
//  else
//    mParentJoint->mParentBodyNode = NULL;
}

void BodyNode::aggregateGenCoords(std::vector<GenCoord*>* _genCoords) {
  assert(mParentJoint);
  for (int i = 0; i < mParentJoint->getNumGenCoords(); ++i) {
    mParentJoint->getGenCoord(i)->setSkeletonIndex(_genCoords->size());
    _genCoords->push_back(mParentJoint->getGenCoord(i));
  }
}

void BodyNode::draw(renderer::RenderInterface* _ri,
                    const Eigen::Vector4d& _color,
                    bool _useDefaultColor,
                    int _depth) const {
  if (_ri == NULL)
    return;

  _ri->pushMatrix();

  // render the self geometry
  mParentJoint->applyGLTransform(_ri);

  _ri->pushName((unsigned)mID);
  for (int i = 0; i < mVizShapes.size(); i++) {
    _ri->pushMatrix();
    mVizShapes[i]->draw(_ri, _color, _useDefaultColor);
    _ri->popMatrix();
  }
  _ri->popName();

  // render the subtree
  for (unsigned int i = 0; i < mChildBodyNodes.size(); i++) {
    mChildBodyNodes[i]->draw(_ri, _color, _useDefaultColor);
  }

  _ri->popMatrix();
}

void BodyNode::drawMarkers(renderer::RenderInterface* _ri,
                           const Eigen::Vector4d& _color,
                           bool _useDefaultColor) const {
  if (!_ri)
    return;

  _ri->pushMatrix();

  mParentJoint->applyGLTransform(_ri);

  // render the corresponding mMarkerss
  for (unsigned int i = 0; i < mMarkers.size(); i++)
    mMarkers[i]->draw(_ri, true, _color, _useDefaultColor);

  for (unsigned int i = 0; i < mChildBodyNodes.size(); i++)
    mChildBodyNodes[i]->drawMarkers(_ri, _color, _useDefaultColor);

  _ri->popMatrix();
}

//==============================================================================
void BodyNode::updateTransform()
{
  // Update parent joint's local transformation
  mParentJoint->updateLocalTransform();

  // Compute world transform
  if (mParentBodyNode)
    mW = mParentBodyNode->mW * mParentJoint->mT;
  else
    mW = mParentJoint->mT;

  // Verification
  assert(math::verifyTransform(mW));

  // Update parent joint's local Jacobian
  mParentJoint->updateLocalJacobian();
}

//==============================================================================
void BodyNode::updateVelocity()
{
  // Transmit velocity of parent body to this body
  if (mParentBodyNode)
    mV = math::AdInvT(mParentJoint->mT, mParentBodyNode->mV);
  else
    mV.setZero();

  // Add parent joint's velocity
  mParentJoint->addVelocityTo(mV);

  // Verification
  assert(!math::isNan(mV));
}

//==============================================================================
void BodyNode::updatePartialAcceleration()
{
  // Update parent joint's time derivative of local Jacobian
  mParentJoint->updateLocalJacobianTimeDeriv();

  // Compute partial acceleration
  mParentJoint->setPartialAccelerationTo(mPartialAcceleration, mV);
}

//==============================================================================
void BodyNode::updateAcceleration()
{
  // Transmit acceleration of parent body to this body
  if (mParentBodyNode)
  {
    mA = math::AdInvT(mParentJoint->mT, mParentBodyNode->mA)
         + mPartialAcceleration;
  }
  else
  {
    mA = mPartialAcceleration;
  }

  // Add parent joint's acceleration to this body
  mParentJoint->addAccelerationTo(mA);

  // Verification
  assert(!math::isNan(mA));
}

//==============================================================================
void BodyNode::updateBodyForce(const Eigen::Vector3d& _gravity,
                               bool _withExternalForces)
{
  // Gravity force
  if (mGravityMode == true)
    mFgravity.noalias() = mI * math::AdInvRLinear(mW, _gravity);
  else
    mFgravity.setZero();

  // Inertial force
  mF.noalias() = mI * mA;

  // External force
  if (_withExternalForces)
    mF -= mFext;

  // Verification
  assert(!math::isNan(mF));

  // Gravity force
  mF -= mFgravity;

  // Coriolis force
  mF -= math::dad(mV, mI * mV);

  //
  for (std::vector<BodyNode*>::iterator iChildBody = mChildBodyNodes.begin();
       iChildBody != mChildBodyNodes.end(); ++iChildBody)
  {
    Joint* childJoint = (*iChildBody)->getParentJoint();
    assert(childJoint != NULL);

    mF += math::dAdInvT(childJoint->getLocalTransform(),
                        (*iChildBody)->getBodyForce());
  }

  // TODO(JS): mWrench and mF are duplicated. Remove one of them.
  mParentJoint->mWrench = mF;

  // Verification
  assert(!math::isNan(mF));
}

//==============================================================================
void BodyNode::updateGeneralizedForce(bool _withDampingForces)
{
  assert(mParentJoint != NULL);

  size_t dof = mParentJoint->getNumGenCoords();

  if (dof > 0)
  {
    const math::Jacobian& J = mParentJoint->getLocalJacobian();

    //    if (_withDampingForces)
    //        mF -= mFDamp;

    assert(!math::isNan(J.transpose()*mF));

    mParentJoint->setGenForces(J.transpose()*mF);
  }
}

//==============================================================================
void BodyNode::updateArtInertia(double _timeStep)
{
  // Set spatial inertia to the articulated body inertia
  mArtInertia = mI;
  mArtInertiaImplicit = mI;

  // and add child articulated body inertia
  for (std::vector<BodyNode*>::const_iterator it = mChildBodyNodes.begin();
       it != mChildBodyNodes.end(); ++it)
  {
    (*it)->getParentJoint()->addChildArtInertiaTo(
          mArtInertia, (*it)->mArtInertia);
    (*it)->getParentJoint()->addChildArtInertiaImplicitTo(
          mArtInertiaImplicit, (*it)->mArtInertiaImplicit);
  }

  // Verification
  assert(!math::isNan(mArtInertia));
  assert(!math::isNan(mArtInertiaImplicit));

  // Update parent joint's inverse of projected articulated body inertia
  mParentJoint->updateInvProjArtInertia(mArtInertia);
  mParentJoint->updateInvProjArtInertiaImplicit(mArtInertiaImplicit, _timeStep);

  // Verification
  assert(!math::isNan(mArtInertia));
  assert(!math::isNan(mArtInertiaImplicit));
}

//==============================================================================
void BodyNode::updateBiasForce(const Eigen::Vector3d& _gravity,
                               double _timeStep)
{
  // Gravity force
  if (mGravityMode == true)
    mFgravity.noalias() = mI * math::AdInvRLinear(mW, _gravity);
  else
    mFgravity.setZero();

  // Set bias force
  mBiasForce = -math::dad(mV, mI * mV) - mFext - mFgravity;

  // Verifycation
  assert(!math::isNan(mBiasForce));

  // And add child bias force
  for (std::vector<BodyNode*>::const_iterator it = mChildBodyNodes.begin();
       it != mChildBodyNodes.end(); ++it)
  {
    (*it)->getParentJoint()->addChildBiasForceTo(mBiasForce,
                                                 (*it)->mArtInertiaImplicit,
                                                 (*it)->mBiasForce,
                                                 (*it)->mPartialAcceleration);
  }

  // Verification
  assert(!math::isNan(mBiasForce));

  // Update parent joint's total force with implicit joint damping and spring
  // forces
  mParentJoint->updateTotalForce(
        mArtInertiaImplicit * mPartialAcceleration + mBiasForce, _timeStep);
}

//==============================================================================
void BodyNode::updateJointAndBodyAcceleration()
{
  if (mParentBodyNode)
  {
    //
    mParentJoint->updateAcceleration(mArtInertiaImplicit, mParentBodyNode->mA);

    // Transmit spatial acceleration of parent body to this body
    mA = math::AdInvT(mParentJoint->mT, mParentBodyNode->mA)
         + mPartialAcceleration;
  }
  else
  {
    //
    mParentJoint->updateAcceleration(mArtInertiaImplicit,
                                     Eigen::Vector6d::Zero());

    // Transmit spatial acceleration of parent body to this body
    mA = mPartialAcceleration;
  }

  // Add parent joint's acceleration to this body
  mParentJoint->addAccelerationTo(mA);

  // Verify the spatial acceleration of this body
  assert(!math::isNan(mA));
}

//==============================================================================
void BodyNode::updateTransmittedForce()
{
  mF = mBiasForce;
  mF.noalias() += mArtInertiaImplicit * mA;

  // TODO(JS): mWrench and mF are duplicated. Remove one of them.
  mParentJoint->mWrench = mF;

  assert(!math::isNan(mF));
}

void BodyNode::setInertia(double _Ixx, double _Iyy, double _Izz,
                          double _Ixy, double _Ixz, double _Iyz) {
  assert(_Ixx >= 0.0);
  assert(_Iyy >= 0.0);
  assert(_Izz >= 0.0);

  mIxx = _Ixx;
  mIyy = _Iyy;
  mIzz = _Izz;

  mIxy = _Ixy;
  mIxz = _Ixz;
  mIyz = _Iyz;

  _updateGeralizedInertia();
}

void BodyNode::setLocalCOM(const Eigen::Vector3d& _com) {
  mCenterOfMass = _com;
  _updateGeralizedInertia();
}

const Eigen::Vector3d& BodyNode::getLocalCOM() const {
  return mCenterOfMass;
}

Eigen::Vector3d BodyNode::getWorldCOM() const {
  return mW * mCenterOfMass;
}

Eigen::Vector3d BodyNode::getWorldCOMVelocity() const {
  return getWorldVelocity(mCenterOfMass, true).tail<3>();
}

Eigen::Vector3d BodyNode::getWorldCOMAcceleration() const {
  return getWorldAcceleration(mCenterOfMass, true).tail<3>();
}

Eigen::Matrix6d BodyNode::getInertia() const {
  return mI;
}

int BodyNode::getSkeletonIndex() const {
  return mSkelIndex;
}

void BodyNode::addVisualizationShape(Shape* _p) {
  mVizShapes.push_back(_p);
}

int BodyNode::getNumVisualizationShapes() const {
  return mVizShapes.size();
}

Shape* BodyNode::getVisualizationShape(int _idx) const {
  return mVizShapes[_idx];
}

void BodyNode::addCollisionShape(Shape* _p) {
  mColShapes.push_back(_p);
}

int BodyNode::getNumCollisionShapes() const {
  return mColShapes.size();
}

Shape* BodyNode::getCollisionShape(int _idx) const {
  return mColShapes[_idx];
}

Skeleton* BodyNode::getSkeleton() const {
  return mSkeleton;
}

void BodyNode::setParentJoint(Joint* _joint) {
  mParentJoint = _joint;
}

Joint* BodyNode::getParentJoint() const {
  return mParentJoint;
}

//==============================================================================
void BodyNode::addExtForce(const Eigen::Vector3d& _force,
                           const Eigen::Vector3d& _offset,
                           bool _isForceLocal,
                           bool _isOffsetLocal)
{
  Eigen::Isometry3d T = Eigen::Isometry3d::Identity();
  Eigen::Vector6d F = Eigen::Vector6d::Zero();

  if (_isOffsetLocal)
    T.translation() = _offset;
  else
    T.translation() = getWorldTransform().inverse() * _offset;

  if (_isForceLocal)
    F.tail<3>() = _force;
  else
    F.tail<3>() = mW.linear().transpose() * _force;

  mFext += math::dAdInvT(T, F);
}

//==============================================================================
void BodyNode::setExtForce(const Eigen::Vector3d& _force,
                           const Eigen::Vector3d& _offset,
                           bool _isForceLocal, bool _isOffsetLocal)
{
  Eigen::Isometry3d T = Eigen::Isometry3d::Identity();
  Eigen::Vector6d F = Eigen::Vector6d::Zero();

  if (_isOffsetLocal)
    T.translation() = _offset;
  else
    T.translation() = getWorldTransform().inverse() * _offset;

  if (_isForceLocal)
    F.tail<3>() = _force;
  else
    F.tail<3>() = mW.linear().transpose() * _force;

  mFext = math::dAdInvT(T, F);
}

//==============================================================================
void BodyNode::addExtTorque(const Eigen::Vector3d& _torque, bool _isLocal)
{
  if (_isLocal)
    mFext.head<3>() += _torque;
  else
    mFext.head<3>() += mW.linear().transpose() * _torque;
}

//==============================================================================
void BodyNode::setExtTorque(const Eigen::Vector3d& _torque, bool _isLocal)
{
  if (_isLocal)
    mFext.head<3>() = _torque;
  else
    mFext.head<3>() = mW.linear().transpose() * _torque;
}

const Eigen::Vector6d& BodyNode::getExternalForceLocal() const {
  return mFext;
}

Eigen::Vector6d BodyNode::getExternalForceGlobal() const {
  return math::dAdInvT(mW, mFext);
}

//==============================================================================
void BodyNode::addConstraintImpulse(const Eigen::Vector3d& _constImp,
                                    const Eigen::Vector3d& _offset,
                                    bool _isImpulseLocal,
                                    bool _isOffsetLocal)
{
  // TODO(JS): Add contact sensor data here (DART 4.1)

  Eigen::Isometry3d T = Eigen::Isometry3d::Identity();
  Eigen::Vector6d F = Eigen::Vector6d::Zero();

  if (_isOffsetLocal)
    T.translation() = _offset;
  else
    T.translation() = getWorldTransform().inverse() * _offset;

  if (_isImpulseLocal)
    F.tail<3>() = _constImp;
  else
    F.tail<3>() = mW.linear().transpose() * _constImp;

  mConstraintImpulse += math::dAdInvT(T, F);
}

//==============================================================================
void BodyNode::clearConstraintImpulse()
{
  mDelV.setZero();
  mBiasImpulse.setZero();
  mConstraintImpulse.setZero();
  mImpF.setZero();

  // TODO(JS): Need to clear this API
  mParentJoint->clearConstraintImpulse();
  mParentJoint->setConstraintImpulses(
        Eigen::VectorXd::Zero(mParentJoint->getNumGenCoords()));
  mParentJoint->setVelsChange(
        Eigen::VectorXd::Zero(mParentJoint->getNumGenCoords()));
}

const Eigen::Vector6d& BodyNode::getBodyForce() const {
  return mF;
}

//==============================================================================
void BodyNode::setConstraintImpulse(const Eigen::Vector6d& _constImp)
{
  assert(!math::isNan(_constImp));
  mConstraintImpulse = _constImp;
}

//==============================================================================
void BodyNode::addConstraintImpulse(const Eigen::Vector6d& _constImp)
{
  assert(!math::isNan(_constImp));
  mConstraintImpulse += _constImp;
}

//==============================================================================
const Eigen::Vector6d& BodyNode::getConstraintImpulse() const
{
  return mConstraintImpulse;
}

double BodyNode::getKineticEnergy() const {
  return 0.5 * mV.dot(mI * mV);
}

double BodyNode::getPotentialEnergy(
    const Eigen::Vector3d& _gravity) const {
  return -mMass * mW.translation().dot(_gravity);
}

Eigen::Vector3d BodyNode::getLinearMomentum() const {
  return (mI * mV).tail<3>();
}

Eigen::Vector3d BodyNode::getAngularMomentum(const Eigen::Vector3d& _pivot) {
  Eigen::Isometry3d T = Eigen::Isometry3d::Identity();
  T.translation() = _pivot;
  return math::dAdT(T, mI * mV).head<3>();
}

//==============================================================================
bool BodyNode::isImpulseReponsible() const
{
  // Should be called at BodyNode::init()
  // TODO(JS): Once hybrid dynamics is implemented, we should consider joint
  //           type of parent joint.
  if (mSkeleton->isMobile() && getNumDependentGenCoords() > 0)
    return true;
  else
    return false;
}

//==============================================================================
void BodyNode::updateBiasImpulse()
{
  // Update impulsive bias force
  mBiasImpulse = -mConstraintImpulse;
//  assert(mImpFext == Eigen::Vector6d::Zero());

  // And add child bias impulse
  for (std::vector<BodyNode*>::const_iterator it = mChildBodyNodes.begin();
       it != mChildBodyNodes.end(); ++it)
  {
    (*it)->getParentJoint()->addChildBiasImpulseTo(mBiasImpulse,
                                                   (*it)->mArtInertia,
                                                   (*it)->mBiasImpulse);
  }

  // Verification
  assert(!math::isNan(mBiasImpulse));

  // Update parent joint's total force
  mParentJoint->updateTotalImpulse(mBiasImpulse);
}

//==============================================================================
void BodyNode::updateJointVelocityChange()
{
  if (mParentBodyNode)
  {
    //
    mParentJoint->updateVelocityChange(mArtInertia, mParentBodyNode->mDelV);

    // Transmit spatial acceleration of parent body to this body
    mDelV = math::AdInvT(mParentJoint->mT, mParentBodyNode->mDelV);
  }
  else
  {
    //
    mParentJoint->updateVelocityChange(mArtInertia, Eigen::Vector6d::Zero());

    // Transmit spatial acceleration of parent body to this body
    mDelV.setZero();
  }

  // Add parent joint's acceleration to this body
  mParentJoint->addVelocityChangeTo(mDelV);

  // Verify the spatial velocity change of this body
  assert(!math::isNan(mDelV));
}

//==============================================================================
//void BodyNode::updateBodyVelocityChange()
//{
//  if (mParentJoint->getNumGenCoords() > 0)
//    mDelV = mParentJoint->getLocalJacobian() * mParentJoint->getVelsChange();
//  else
//    mDelV.setZero();

//  if (mParentBodyNode)
//  {
//    mDelV += math::AdInvT(mParentJoint->getLocalTransform(),
//                          mParentBodyNode->mDelV);
//  }

//  assert(!math::isNan(mDelV));
//}

//==============================================================================
void BodyNode::updateBodyImpForceFwdDyn()
{
  mImpF = mBiasImpulse;
  mImpF.noalias() += mArtInertia * mDelV;
  assert(!math::isNan(mImpF));
}

//==============================================================================
void BodyNode::updateConstrainedJointAndBodyAcceleration(double _timeStep)
{
  // 1. dq = dq + del_dq
  mParentJoint->updateVelocityWithVelocityChange();

  // 2. ddq = ddq + del_dq / dt
  mParentJoint->updateAccelerationWithVelocityChange(_timeStep);

  // 3. tau = tau + imp / dt
  mParentJoint->updateForceWithImpulse(_timeStep);
}

//==============================================================================
void BodyNode::updateConstrainedTransmittedForce(double _timeStep)
{
  ///
  mA += mDelV / _timeStep;

  ///
  mF += _timeStep * mImpF;
}

//==============================================================================
void BodyNode::aggregateCoriolisForceVector(Eigen::VectorXd* _C)
{
  aggregateCombinedVector(_C, Eigen::Vector3d::Zero());
}

void BodyNode::aggregateGravityForceVector(Eigen::VectorXd* _g,
                                           const Eigen::Vector3d& _gravity) {
  if (mGravityMode == true)
    mG_F = mI * math::AdInvRLinear(mW, _gravity);
  else
    mG_F.setZero();

  for (std::vector<BodyNode*>::const_iterator it = mChildBodyNodes.begin();
       it != mChildBodyNodes.end(); ++it) {
    mG_F += math::dAdInvT((*it)->mParentJoint->getLocalTransform(),
                          (*it)->mG_F);
  }

  int nGenCoords = mParentJoint->getNumGenCoords();
  if (nGenCoords > 0) {
    Eigen::VectorXd g = -(mParentJoint->getLocalJacobian().transpose() * mG_F);
    int iStart = mParentJoint->getGenCoord(0)->getSkeletonIndex();
    _g->segment(iStart, nGenCoords) = g;
  }
}

//==============================================================================
void BodyNode::updateCombinedVector()
{
  if (mParentBodyNode)
  {
    mCg_dV = math::AdInvT(mParentJoint->getLocalTransform(),
                          mParentBodyNode->mCg_dV) + mPartialAcceleration;
  }
  else
  {
    mCg_dV = mPartialAcceleration;
  }
}

//==============================================================================
void BodyNode::aggregateCombinedVector(Eigen::VectorXd* _Cg,
                                       const Eigen::Vector3d& _gravity)
{
  // H(i) = I(i) * W(i) -
  //        dad{V}(I(i) * V(i)) + sum(k \in children) dAd_{T(i,j)^{-1}}(H(k))
  if (mGravityMode == true)
    mFgravity = mI * math::AdInvRLinear(mW, _gravity);
  else
    mFgravity.setZero();

  mCg_F = mI * mCg_dV;
  mCg_F -= mFgravity;
  mCg_F -= math::dad(mV, mI * mV);

  for (std::vector<BodyNode*>::iterator it = mChildBodyNodes.begin();
       it != mChildBodyNodes.end(); ++it)
  {
    mCg_F += math::dAdInvT((*it)->getParentJoint()->mT, (*it)->mCg_F);
  }

  int nGenCoords = mParentJoint->getNumGenCoords();
  if (nGenCoords > 0)
  {
    Eigen::VectorXd Cg
        = mParentJoint->getLocalJacobian().transpose() * mCg_F;
    int iStart = mParentJoint->getGenCoord(0)->getSkeletonIndex();
    _Cg->segment(iStart, nGenCoords) = Cg;
  }
}

void BodyNode::aggregateExternalForces(Eigen::VectorXd* _Fext) {
  mFext_F = mFext;

  for (std::vector<BodyNode*>::const_iterator it = mChildBodyNodes.begin();
       it != mChildBodyNodes.end(); ++it) {
    mFext_F += math::dAdInvT((*it)->mParentJoint->getLocalTransform(),
                             (*it)->mFext_F);
  }

  int nGenCoords = mParentJoint->getNumGenCoords();
  if (nGenCoords > 0) {
    Eigen::VectorXd Fext = mParentJoint->getLocalJacobian().transpose()*mFext_F;
    int iStart = mParentJoint->getGenCoord(0)->getSkeletonIndex();
    _Fext->segment(iStart, nGenCoords) = Fext;
  }
}

void BodyNode::updateMassMatrix() {
  mM_dV.setZero();
  int dof = mParentJoint->getNumGenCoords();
  if (dof > 0) {
    mM_dV.noalias() += mParentJoint->getLocalJacobian() *
                       mParentJoint->getGenAccs();
    assert(!math::isNan(mM_dV));
  }
  if (mParentBodyNode)
    mM_dV += math::AdInvT(mParentJoint->getLocalTransform(),
                          mParentBodyNode->mM_dV);
  assert(!math::isNan(mM_dV));
}

//==============================================================================
void BodyNode::aggregateMassMatrix(Eigen::MatrixXd* _MCol, int _col)
{
  //
  mM_F.noalias() = mI * mM_dV;

  // Verification
  assert(!math::isNan(mM_F));

  //
  for (std::vector<BodyNode*>::const_iterator it = mChildBodyNodes.begin();
       it != mChildBodyNodes.end(); ++it)
  {
    mM_F += math::dAdInvT((*it)->getParentJoint()->getLocalTransform(),
                          (*it)->mM_F);
  }

  // Verification
  assert(!math::isNan(mM_F));

  //
  int dof = mParentJoint->getNumGenCoords();
  if (dof > 0)
  {
    int iStart = mParentJoint->getGenCoord(0)->getSkeletonIndex();
    _MCol->block(iStart, _col, dof, 1).noalias() =
        mParentJoint->getLocalJacobian().transpose() * mM_F;
  }
}

//==============================================================================
void BodyNode::aggregateAugMassMatrix(Eigen::MatrixXd* _MCol, int _col,
                                      double _timeStep)
{
  //
  mM_F.noalias() = mI * mM_dV;

  // Verification
  assert(!math::isNan(mM_F));

  //
  for (std::vector<BodyNode*>::const_iterator it = mChildBodyNodes.begin();
       it != mChildBodyNodes.end(); ++it)
  {
    mM_F += math::dAdInvT((*it)->getParentJoint()->getLocalTransform(),
                          (*it)->mM_F);
  }

  // Verification
  assert(!math::isNan(mM_F));

  //
  int dof = mParentJoint->getNumGenCoords();
  if (dof > 0)
  {
    Eigen::MatrixXd K = Eigen::MatrixXd::Zero(dof, dof);
    Eigen::MatrixXd D = Eigen::MatrixXd::Zero(dof, dof);
    for (int i = 0; i < dof; ++i)
    {
      K(i, i) = mParentJoint->getSpringStiffness(i);
      D(i, i) = mParentJoint->getDampingCoefficient(i);
    }

    int iStart = mParentJoint->getGenCoord(0)->getSkeletonIndex();

    _MCol->block(iStart, _col, dof, 1).noalias()
        = mParentJoint->getLocalJacobian().transpose() * mM_F
          + D * (_timeStep * mParentJoint->getGenAccs())
          + K * (_timeStep * _timeStep * mParentJoint->getGenAccs());
  }
}

//==============================================================================
void BodyNode::updateInvMassMatrix()
{
  //
  mInvM_c.setZero();

  //
  for (std::vector<BodyNode*>::const_iterator it = mChildBodyNodes.begin();
       it != mChildBodyNodes.end(); ++it)
  {
    (*it)->getParentJoint()->addChildBiasForceForInvMassMatrix(
          mInvM_c, (*it)->mArtInertia, (*it)->mInvM_c);
  }

  // Verification
  assert(!math::isNan(mInvM_c));

  // Update parent joint's total force for inverse mass matrix
  mParentJoint->updateTotalForceForInvMassMatrix(mInvM_c);
}

//==============================================================================
void BodyNode::updateInvAugMassMatrix()
{
  //
  mInvM_c.setZero();

  //
  for (std::vector<BodyNode*>::const_iterator it = mChildBodyNodes.begin();
       it != mChildBodyNodes.end(); ++it)
  {
    (*it)->getParentJoint()->addChildBiasForceForInvAugMassMatrix(
          mInvM_c, (*it)->mArtInertiaImplicit, (*it)->mInvM_c);
  }

  // Verification
  assert(!math::isNan(mInvM_c));

  // Update parent joint's total force for inverse mass matrix
  mParentJoint->updateTotalForceForInvMassMatrix(mInvM_c);
}

//==============================================================================
void BodyNode::aggregateInvMassMatrix(Eigen::MatrixXd* _InvMCol, int _col)
{
  if (mParentBodyNode)
  {
    //
    mParentJoint->getInvMassMatrixSegment(
          *_InvMCol, _col, mArtInertia, mParentBodyNode->mInvM_U);

    //
    mInvM_U = math::AdInvT(mParentJoint->mT, mParentBodyNode->mInvM_U);
  }
  else
  {
    //
    mParentJoint->getInvMassMatrixSegment(
          *_InvMCol, _col, mArtInertia, Eigen::Vector6d::Zero());

    //
    mInvM_U.setZero();
  }

  //
  mParentJoint->addInvMassMatrixSegmentTo(mInvM_U);
}

//==============================================================================
void BodyNode::aggregateInvAugMassMatrix(Eigen::MatrixXd* _InvMCol, int _col,
                                         double /*_timeStep*/)
{
  if (mParentBodyNode)
  {
    //
    mParentJoint->getInvAugMassMatrixSegment(
          *_InvMCol, _col, mArtInertiaImplicit, mParentBodyNode->mInvM_U);

    //
    mInvM_U = math::AdInvT(mParentJoint->mT, mParentBodyNode->mInvM_U);
  }
  else
  {
    //
    mParentJoint->getInvAugMassMatrixSegment(
          *_InvMCol, _col, mArtInertiaImplicit, Eigen::Vector6d::Zero());

    //
    mInvM_U.setZero();
  }

  //
  mParentJoint->addInvMassMatrixSegmentTo(mInvM_U);
}

//==============================================================================
void BodyNode::_updateBodyJacobian()
{
  //--------------------------------------------------------------------------
  // Jacobian update
  //
  // J = | J1 J2 ... Jn |
  //   = | Ad(T(i,i-1), J_parent) J_local |
  //
  //   J_parent: (6 x parentDOF)
  //    J_local: (6 x localDOF)
  //         Ji: (6 x 1) se3
  //          n: number of dependent coordinates
  //--------------------------------------------------------------------------

  const int localDof     = mParentJoint->getNumGenCoords();
  const int ascendantDof = getNumDependentGenCoords() - localDof;

  // Parent Jacobian
  if (mParentBodyNode)
  {
    assert(mParentBodyNode->getBodyJacobian().cols() +
           mParentJoint->getNumGenCoords() == mBodyJacobian.cols());

    assert(mParentJoint);
    mBodyJacobian.leftCols(ascendantDof) =
        math::AdInvTJac(mParentJoint->getLocalTransform(),
                        mParentBodyNode->getBodyJacobian());
  }

  // Local Jacobian
  mBodyJacobian.rightCols(localDof) = mParentJoint->getLocalJacobian();

  mIsBodyJacobianDirty = false;
}

void BodyNode::_updateBodyJacobianTimeDeriv()
{
  //--------------------------------------------------------------------------
  // Jacobian first derivative update
  //
  // dJ = | dJ1 dJ2 ... dJn |
  //   = | Ad(T(i,i-1), dJ_parent) dJ_local |
  //
  //   dJ_parent: (6 x parentDOF)
  //    dJ_local: (6 x localDOF)
  //         dJi: (6 x 1) se3
  //          n: number of dependent coordinates
  //--------------------------------------------------------------------------

  const int numLocalDOFs = mParentJoint->getNumGenCoords();
  const int numParentDOFs = getNumDependentGenCoords() - numLocalDOFs;
  math::Jacobian J = getBodyJacobian();

  // Parent Jacobian
  if (mParentBodyNode) {
    assert(mParentBodyNode->mBodyJacobianTimeDeriv.cols()
           + mParentJoint->getNumGenCoords() == mBodyJacobianTimeDeriv.cols());

    assert(mParentJoint);
    mBodyJacobianTimeDeriv.leftCols(numParentDOFs)
        = math::AdInvTJac(mParentJoint->getLocalTransform(),
                          mParentBodyNode->mBodyJacobianTimeDeriv);
    for (int i = 0; i < numParentDOFs; ++i)
      mBodyJacobianTimeDeriv.col(i) -= math::ad(mV, J.col(i));
  }

  // Local Jacobian
  mBodyJacobianTimeDeriv.rightCols(numLocalDOFs) =
      mParentJoint->getLocalJacobianTimeDeriv();

  mIsBodyJacobianTimeDerivDirty = false;
}

void BodyNode::_updateGeralizedInertia() {
  // G = | I - m*[r]*[r]   m*[r] |
  //     |        -m*[r]     m*I |

  // m*r
  double mr0 = mMass * mCenterOfMass[0];
  double mr1 = mMass * mCenterOfMass[1];
  double mr2 = mMass * mCenterOfMass[2];

  // m*[r]*[r]
  double mr0r0 = mr0 * mCenterOfMass[0];
  double mr1r1 = mr1 * mCenterOfMass[1];
  double mr2r2 = mr2 * mCenterOfMass[2];
  double mr0r1 = mr0 * mCenterOfMass[1];
  double mr1r2 = mr1 * mCenterOfMass[2];
  double mr2r0 = mr2 * mCenterOfMass[0];

  // Top left corner (3x3)
  mI(0, 0) =  mIxx + mr1r1 + mr2r2;
  mI(1, 1) =  mIyy + mr2r2 + mr0r0;
  mI(2, 2) =  mIzz + mr0r0 + mr1r1;
  mI(0, 1) =  mIxy - mr0r1;
  mI(0, 2) =  mIxz - mr2r0;
  mI(1, 2) =  mIyz - mr1r2;

  // Top right corner (3x3)
  mI(1, 5) = -mr0;
  mI(0, 5) =  mr1;
  mI(0, 4) = -mr2;
  mI(2, 4) =  mr0;
  mI(2, 3) = -mr1;
  mI(1, 3) =  mr2;
  assert(mI(0, 3) == 0.0);
  assert(mI(1, 4) == 0.0);
  assert(mI(2, 5) == 0.0);

  // Bottom right corner (3x3)
  mI(3, 3) =  mMass;
  mI(4, 4) =  mMass;
  mI(5, 5) =  mMass;
  assert(mI(3, 4) == 0.0);
  assert(mI(3, 5) == 0.0);
  assert(mI(4, 5) == 0.0);

  mI.triangularView<Eigen::StrictlyLower>() = mI.transpose();
}

void BodyNode::clearExternalForces() {
  mFext.setZero();
}

void BodyNode::fitWorldTransformParentJointImpl(
    const Eigen::Isometry3d& _target, bool _jointLimit)
{
  Joint* parentJoint = getParentJoint();
  size_t dof = parentJoint->getNumGenCoords();

  if (dof == 0)
    return;

  optimizer::Problem prob(dof);

  // Use the current joint configuration as initial guess
  prob.setInitialGuess(parentJoint->getConfigs());

  // Objective function
  TransformObjFunc obj(this, _target, mSkeleton);
  prob.setObjective(&obj);

  // Joint limit
  if (_jointLimit)
  {
    prob.setLowerBounds(parentJoint->getConfigsMin());
    prob.setUpperBounds(parentJoint->getConfigsMax());
  }

  // Solve with gradient-free local minima algorithm
  optimizer::NloptSolver solver(&prob, NLOPT_LN_BOBYQA);
  solver.solve();

  // Set optimal configuration of the parent joint
  Eigen::VectorXd jointQ = prob.getOptimalSolution();
  parentJoint->setConfigs(jointQ, true, true, true);
}

void BodyNode::fitWorldTransformAncestorJointsImpl(
    const Eigen::Isometry3d& /*_target*/, bool /*_jointLimit*/)
{
  dterr << "Not implemented yet.\n";
}

void BodyNode::fitWorldTransformAllJointsImpl(
    const Eigen::Isometry3d& /*_target*/, bool /*_jointLimit*/)
{
  dterr << "Not implemented yet.\n";
}

BodyNode::TransformObjFunc::TransformObjFunc(
    BodyNode* _body, const Eigen::Isometry3d& _T, Skeleton* _skeleton)
  : Function(), mBodyNode(_body), mT(_T), mSkeleton(_skeleton)
{
}

BodyNode::TransformObjFunc::~TransformObjFunc()
{
}

double BodyNode::TransformObjFunc::eval(Eigen::Map<const Eigen::VectorXd>& _x)
{
  assert(mBodyNode->getParentJoint()->getNumGenCoords() == _x.size());

  // Update forward kinematics information with _x
  // We are just insterested in transformation of mBodyNode
  mBodyNode->getParentJoint()->setConfigs(_x, true, false, false);

  // Compute and return the geometric distance between body node transformation
  // and target transformation
  Eigen::Isometry3d bodyT = mBodyNode->getWorldTransform();
  Eigen::Vector6d dist = math::logMap(bodyT.inverse() * mT);
  return dist.dot(dist);
}

BodyNode::VelocityObjFunc::VelocityObjFunc(BodyNode* _body,
                                           const Eigen::Vector3d& _vel,
                                           VelocityType _velType,
                                           Skeleton* _skeleton)
  : Function(),
    mBodyNode(_body),
    mVelocityType(_velType),
    mSkeleton(_skeleton)
{
  if (mVelocityType == VT_LINEAR)
  {
    mVelocity.head<3>() = mBodyNode->getWorldVelocity().head<3>();
    mVelocity.tail<3>() = _vel;
  }
  else  // mVelocityType == VT_ANGULAR
  {
    mVelocity.head<3>() = _vel;
    mVelocity.tail<3>() = mBodyNode->getWorldVelocity().tail<3>();
  }
}

BodyNode::VelocityObjFunc::~VelocityObjFunc()
{
}

double BodyNode::VelocityObjFunc::eval(Eigen::Map<const Eigen::VectorXd>& _x)
{
  assert(mBodyNode->getParentJoint()->getNumGenCoords() == _x.size());

  // Update forward kinematics information with _x
  // We are just insterested in spacial velocity of mBodyNode
  mBodyNode->getParentJoint()->setGenVels(_x, true, false);

  // Compute and return the geometric distance between body node transformation
  // and target transformation
  Eigen::Vector6d diff = mBodyNode->getWorldVelocity() - mVelocity;
  return diff.dot(diff);
}

}  // namespace dynamics
}  // namespace dart