#ifndef MODULE_PLATFORM_GAZEBO_HPP
#define MODULE_PLATFORM_GAZEBO_HPP

#include <nuclear>

namespace module::platform {

    class Gazebo : public NUClear::Reactor {

    public:
        /// @brief Called by the powerplant to build and setup the Gazebo reactor.
        explicit Gazebo(std::unique_ptr<NUClear::Environment> environment);

    private:
        struct {
            std::string simulator_name;
            std::string model_name;
            double clock_smoothing = 0.0;
        } config;

        double sim_time;
        double real_time;
    };
}  // namespace module::platform

#endif  // MODULE_PLATFORM_GAZEBO_HPP
