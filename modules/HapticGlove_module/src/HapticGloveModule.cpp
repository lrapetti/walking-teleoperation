/**
 * @file HapticGloveModule.cpp
 * @authors  Kourosh Darvish <kourosh.darvish@iit.it>
 * @copyright 2020 iCub Facility - Istituto Italiano di Tecnologia
 *            Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 * @date 2020
 */

// YARP
#include <yarp/os/Bottle.h>
#include <yarp/os/LogStream.h>
#include <yarp/os/Property.h>
#include <yarp/os/Stamp.h>

#include <iDynTree/Core/EigenHelpers.h>
#include <iDynTree/yarp/YARPConversions.h>
#include <iDynTree/yarp/YARPEigenConversions.h>

#include <HapticGloveModule.hpp>
#include <Utils.hpp>

HapticGloveModule::HapticGloveModule(){};

HapticGloveModule::~HapticGloveModule(){};

bool HapticGloveModule::configure(yarp::os::ResourceFinder& rf)
{

    yarp::os::Value* value;

    // check if the configuration file is empty
    if (rf.isNull())
    {
        yError() << "[HapticGloveModule::configure] Empty configuration for the HapticGloveModule "
                    "application.";
        return false;
    }
    // set the module name
    std::string name;
    if (!YarpHelper::getStringFromSearchable(rf, "name", name))
    {
        yError() << "[OculusModule::configure] Unable to get a string from a searchable";
        return false;
    }
    setName(name.c_str());

    yarp::os::Bottle& generalOptions = rf.findGroup("GENERAL");
    // get the period
    m_dT = generalOptions.check("samplingTime", yarp::os::Value(0.1)).asDouble();

    // check if move the robot
    m_moveRobot = generalOptions.check("enableMoveRobot", yarp::os::Value(1)).asBool();
    yInfo() << "[HapticGloveModule::configure] move the robot: " << m_moveRobot;

    // check if log the data
    m_enableLogger = generalOptions.check("enableLogger", yarp::os::Value(0)).asBool();
    yInfo() << "[HapticGloveModule::configure] enable the logger: " << m_enableLogger;

    // configure fingers retargeting
    m_leftHandFingers = std::make_unique<FingersRetargeting>();
    yarp::os::Bottle& leftFingersOptions = rf.findGroup("LEFT_FINGERS_RETARGETING");
    leftFingersOptions.append(generalOptions);
    if (!m_leftHandFingers->configure(leftFingersOptions, getName()))
    {
        yError()
            << "[HapticGloveModule::configure] Unable to initialize the left fingers retargeting.";
        return false;
    }

    m_rightHandFingers = std::make_unique<FingersRetargeting>();
    yarp::os::Bottle& rightFingersOptions = rf.findGroup("RIGHT_FINGERS_RETARGETING");
    rightFingersOptions.append(generalOptions);
    if (!m_rightHandFingers->configure(rightFingersOptions, getName()))
    {
        yError()
            << "[HapticGloveModule::configure] Unable to initialize the right fingers retargeting.";
        return false;
    }
    m_timeStarting = 0.0;
    m_timeNow = 0.0;

    m_icubLeftFingerAxisReference.resize(m_leftHandFingers->controlHelper()->getActuatedDoFs());
    m_icubLeftFingerAxisFeedback.resize(m_leftHandFingers->controlHelper()->getActuatedDoFs());
    m_icubLeftFingerJointsReference.resize(m_leftHandFingers->controlHelper()->getNumberOfJoints());
    m_icubLeftFingerJointsFeedback.resize(m_leftHandFingers->controlHelper()->getNumberOfJoints());

    m_icubRightFingerAxisReference.resize(m_rightHandFingers->controlHelper()->getActuatedDoFs());
    m_icubRightFingerAxisFeedback.resize(m_rightHandFingers->controlHelper()->getActuatedDoFs());
    m_icubRightFingerJointsReference.resize(
        m_rightHandFingers->controlHelper()->getNumberOfJoints());
    m_icubRightFingerJointsFeedback.resize(
        m_rightHandFingers->controlHelper()->getNumberOfJoints());

    // open the logger only if all the vecotos sizes are clear.
    if (m_enableLogger)
    {
        if (!openLogger())
        {
            yError() << "[HapticGloveModule::configure] Unable to open the logger";
            return false;
        }
    }

    m_state = HapticGloveFSM::Configured;

    return true;
}

double HapticGloveModule::getPeriod()
{
    return m_dT;
}

bool HapticGloveModule::close()
{
#ifdef ENABLE_LOGGER
    if (m_enableLogger)
    {
        m_logger->flush_available_data();
    }
#endif
    // m_logger.reset();

    return true;
}

bool HapticGloveModule::evaluateRobotFingersReferences()
{
    return true;
}

bool HapticGloveModule::getFeedbacks()
{
    // 1- get the joint ref values from the haptic glove

    // 2- get the feedback from the iCub hands [force (holding force) and tactile
    // information]

    if (!m_leftHandFingers->updateFeedback())
    {
        yError() << "[HapticGloveModule::getFeedbacks()] unable to update the feedback values of "
                    "the left hand fingers.";
    }
    m_leftHandFingers->getFingerAxisMeasuredValues(m_icubLeftFingerAxisFeedback);
    m_leftHandFingers->getFingerJointsMeasuredValues(m_icubLeftFingerJointsFeedback);
    yInfo() << "fingers axis: " << m_icubLeftFingerAxisFeedback.toString();
    yInfo() << "fingers joints: " << m_icubLeftFingerJointsFeedback.toString();

    return true;
}

bool HapticGloveModule::updateModule()
{

    if (!getFeedbacks())
    {
        yError() << "[HapticGloveModule::updateModule] Unable to get the feedback";
        return false;
    }

    if (m_state == HapticGloveFSM::Running)
    {
        m_timeNow = yarp::os::Time::now();

        // 1- Compute the reference values for the iCub hand fingers
        const unsigned noLeftFingersAxis = m_leftHandFingers->controlHelper()->getActuatedDoFs();
        for (unsigned i = 0; i < noLeftFingersAxis; i++)
        {
            m_icubLeftFingerAxisReference(i) = M_PI_4 + M_PI_4 * sin((m_timeNow - m_timeStarting));
            m_icubRightFingerAxisReference(i) = m_icubLeftFingerAxisReference(i);
        }

        // 2- Compute the reference values for the haptic glove, including resistance force and
        // vibrotactile feedback

        // 3- Set the reference joint valued for the iCub hand fingers
        // left hand
        m_leftHandFingers->setFingersAxisReference(m_icubLeftFingerAxisReference);
        m_leftHandFingers->move();

        // right hand
        m_rightHandFingers->setFingersAxisReference(m_icubRightFingerAxisReference);
        m_rightHandFingers->move();

        // 4- Set the reference values for the haptic glove, including resistance force and
        // vibrotactile feedback

#ifdef ENABLE_LOGGER
        if (m_enableLogger)
        {
            m_logger->add(m_logger_prefix + "_time", yarp::os::Time::now());

            /* Left Hand */
            // Axis
            std::vector<double> icubLeftFingerAxisFeedback, icubLeftFingerAxisReference;
            for (unsigned i = 0; i < m_leftHandFingers->controlHelper()->getActuatedDoFs(); i++)
            {
                icubLeftFingerAxisReference.push_back(m_icubLeftFingerAxisReference(i));
                icubLeftFingerAxisFeedback.push_back(m_icubLeftFingerAxisFeedback(i));
            }

            m_logger->add(m_logger_prefix + "_icubLeftFingerAxisFeedback",
                          icubLeftFingerAxisFeedback);
            m_logger->add(m_logger_prefix + "_icubLeftFingerAxisReference",
                          icubLeftFingerAxisReference);
            // Joints
            std::vector<double> icubLeftFingerJointsReference, icubLeftFingerJointsFeedback;
            for (unsigned i = 0; i < m_leftHandFingers->controlHelper()->getNumberOfJoints(); i++)
            {
                icubLeftFingerJointsReference.push_back(m_icubLeftFingerJointsReference(i));
                icubLeftFingerJointsFeedback.push_back(m_icubLeftFingerJointsFeedback(i));
            }
            m_logger->add(m_logger_prefix + "_icubLeftFingerJointsReference",
                          icubLeftFingerJointsReference);
            m_logger->add(m_logger_prefix + "_icubLeftFingerJointsFeedback",
                          icubLeftFingerJointsFeedback);

            /* Right Hand */
            // Axis
            std::vector<double> icubRightFingerAxisFeedback, icubRightFingerAxisReference;
            for (unsigned i = 0; i < m_rightHandFingers->controlHelper()->getActuatedDoFs(); i++)
            {
                icubRightFingerAxisFeedback.push_back(m_icubRightFingerAxisFeedback(i));
                icubRightFingerAxisReference.push_back(m_icubRightFingerAxisReference(i));
            }
            m_logger->add(m_logger_prefix + "_icubRightFingerAxisFeedback",
                          icubRightFingerAxisFeedback);
            m_logger->add(m_logger_prefix + "_icubRightFingerAxisReference",
                          icubRightFingerAxisReference);
            // Joints
            std::vector<double> icubRightFingerJointsReference, icubRightFingerJointsFeedback;
            for (unsigned i = 0; i < m_rightHandFingers->controlHelper()->getNumberOfJoints(); i++)
            {
                icubRightFingerJointsReference.push_back(m_icubRightFingerJointsReference(i));
                icubRightFingerJointsFeedback.push_back(m_icubRightFingerJointsFeedback(i));
            }
            m_logger->add(m_logger_prefix + "_icubRightFingerJointsReference",
                          icubRightFingerJointsReference);
            m_logger->add(m_logger_prefix + "_icubRightFingerJointsFeedback",
                          icubRightFingerJointsFeedback);
            std::cerr << "110 \n";
            m_logger->flush_available_data();
        }
#endif

    } else if (m_state == HapticGloveFSM::Configured)
    {
        // TODO
        m_state = HapticGloveFSM::InPreparation;
    } else if (m_state == HapticGloveFSM::InPreparation)
    {

        m_timeStarting = yarp::os::Time::now();

        // TODO
        m_state = HapticGloveFSM::Running;
        yInfo() << "[HapticGloveModule::updateModule] start the haptic glove module";
        yInfo() << "[HapticGloveModule::updateModule] Running ...";
    }
    // TO

    return true;
}

bool HapticGloveModule::openLogger()
{
#ifdef ENABLE_LOGGER
    std::string currentTime = getTimeDateMatExtension();
    std::string fileName = "HapticGloveModule_" + currentTime + "_log.mat";

    yInfo() << "log file name: " << currentTime << fileName;
    m_logger = XBot::MatLogger2::MakeLogger(fileName);
    m_appender = XBot::MatAppender::MakeInstance();
    m_appender->add_logger(m_logger);
    m_appender->start_flush_thread();

    m_logger->create(m_logger_prefix + "_time", 1);

    m_logger->create(m_logger_prefix + "_icubLeftFingerAxisReference",
                     m_leftHandFingers->controlHelper()->getActuatedDoFs());
    m_logger->create(m_logger_prefix + "_icubLeftFingerAxisFeedback",
                     m_leftHandFingers->controlHelper()->getActuatedDoFs());
    m_logger->create(m_logger_prefix + "_icubRightFingerAxisReference",
                     m_rightHandFingers->controlHelper()->getActuatedDoFs());
    m_logger->create(m_logger_prefix + "_icubRightFingerAxisFeedback",
                     m_rightHandFingers->controlHelper()->getActuatedDoFs());

    m_logger->create(m_logger_prefix + "_icubLeftFingerJointsReference",
                     m_leftHandFingers->controlHelper()->getNumberOfJoints());
    m_logger->create(m_logger_prefix + "_icubLeftFingerJointsFeedback",
                     m_leftHandFingers->controlHelper()->getNumberOfJoints());
    m_logger->create(m_logger_prefix + "_icubRightFingerJointsReference",
                     m_rightHandFingers->controlHelper()->getNumberOfJoints());
    m_logger->create(m_logger_prefix + "_icubRightFingerJointsFeedback",
                     m_rightHandFingers->controlHelper()->getNumberOfJoints());

    m_logger->create(m_logger_prefix + "_loc_joypad_x_y",
                     2); // [x,y] component for robot locomotion
    yInfo() << "[HapticGloveModule::openLogger] Logging is active.";
#else
    yInfo() << "[HapticGloveModule::openLogger] option is not active in CMakeLists.";

#endif
    return true;
}
