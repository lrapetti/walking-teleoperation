
/**
 * Definition of the RobotSkin RPC service
 */
service RobotSkinService {

    /**
     * ercentage dedicated to absolute skin data for providing the vibrotactile feedback.
     * the value is between [0,1]
     * 0 : 0% absolute skin value for the vibrotactile feedback; 100% derivative skin value for the vibrotactile feedback
     * 1 : 100% absolute skin value for the vibrotactile feedback; 0% derivative skin value for the vibrotactile feedback
     *
     * @return true if the procedure was successful, false otherwise
     */
    bool setAbsoluteSkinValuePercentage(1: double value);

}