/*
 * This file is part of the NUbots Codebase.
 *
 * The NUbots Codebase is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The NUbots Codebase is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the NUbots Codebase.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2013 NUBots <nubots@nubots.net>
 */

#include "LUTClassifier.h"
#include "utility/idiom/pimpl_impl.h"

#include "messages/input/Image.h"
#include "messages/input/CameraParameters.h"
#include "messages/input/Sensors.h"
#include "messages/vision/LookUpTable.h"
#include "messages/vision/SaveLookUpTable.h"
#include "messages/support/Configuration.h"

#include "QuexClassifier.h"

#include "Lexer.hpp"

namespace modules {
    namespace vision {

        using messages::input::Image;
        using messages::input::Sensors;
        using messages::input::CameraParameters;
        using messages::vision::LookUpTable;
        using messages::vision::SaveLookUpTable;
        using messages::vision::ObjectClass;
        using messages::vision::ClassifiedImage;
        using messages::support::Configuration;

        // Implement our impl class.
        class LUTClassifier::impl {
        public:
            QuexClassifier quex;

            uint VISUAL_HORIZON_SPACING = 100;
            uint HORIZON_BUFFER = 0;
            uint MINIMUM_VISUAL_HORIZON_SEGMENT_SIZE = 0;
            uint GOAL_FINDER_LINE_SPACING = 100;
            std::vector<double> GOAL_FINDER_DETECTOR_LEVELS = { 2.0 };
            double BALL_SEARCH_FACTOR = 2.0;

            void setParameters(const CameraParameters& cam, const Configuration<LUTClassifier>& config) {
                // Visual horizon detector
                VISUAL_HORIZON_SPACING = cam.effectiveScreenDistancePixels * tan(config["visual_horizon"]["spacing"].as<double>());
                HORIZON_BUFFER =  cam.effectiveScreenDistancePixels * tan(config["visual_horizon"]["horizon_buffer"].as<double>());
                MINIMUM_VISUAL_HORIZON_SEGMENT_SIZE = cam.effectiveScreenDistancePixels * tan(config["visual_horizon"]["minimum_segment_size"].as<double>());

                // // Goal detector
                GOAL_FINDER_LINE_SPACING = cam.effectiveScreenDistancePixels * tan(config["goals"]["spacing"].as<double>());
                GOAL_FINDER_DETECTOR_LEVELS = config["goals"]["detector_levels"].as<std::vector<double>>();

                // // Ball Detector
                double minIntersections = config["ball"]["intersections"].as<double>();
                BALL_SEARCH_FACTOR = 0.1 * cam.pixelsToTanThetaFactor[1] / minIntersections;
            }
        };

        void insertSegments(ClassifiedImage<ObjectClass>& image, std::vector<ClassifiedImage<ObjectClass>::Segment>& segments, bool vertical) {
            ClassifiedImage<ObjectClass>::Segment* previous = nullptr;
            ClassifiedImage<ObjectClass>::Segment* current = nullptr;

            auto& target = vertical ? image.verticalSegments : image.horizontalSegments;

            for (auto& s : segments) {

                // Move in the data
                current = &(target.insert(std::make_pair(s.colour, std::move(s)))->second);

                // Link up the results
                current->previous = previous;
                if(previous) {
                    previous->next = current;
                }

                // Get ready for our next one
                previous = current;
            }
        }

        LUTClassifier::LUTClassifier(std::unique_ptr<NUClear::Environment> environment) : Reactor(std::move(environment)) {

            on<Trigger<Configuration<LUTLocation>>>([this](const Configuration<LUTLocation>& config) {
                emit(std::make_unique<LookUpTable>(config.config.as<LookUpTable>()));
            });

            auto setParams = [this] (const CameraParameters& cam, const Configuration<LUTClassifier>& config) {
                m->setParameters(std::forward<const CameraParameters&>(cam), std::forward<const Configuration<LUTClassifier>&>(config));
            };

            on<Trigger<CameraParameters>, With<Configuration<LUTClassifier>>>(setParams);
            on<With<CameraParameters>, Trigger<Configuration<LUTClassifier>>>(setParams);

            on<Trigger<Image>, With<LookUpTable, Sensors>, Options<Single>>("Classify Image", [this](const Image& image, const LookUpTable& lut, const Sensors& sensors) {

                auto classifiedImage = std::make_unique<ClassifiedImage<ObjectClass>>();

                /**********************************************
                 *                FIND HORIZON                *
                 **********************************************/

                // Element 0 is gradient, element 1 is intercept (confirmed by Jake's Implementation)
                // Coordinate system: 0,0 is the centre of the screen. pos[0] is along the y axis of the
                // camera transform, pos[1] is along the z axis (x points out of the camera)
                arma::vec2 horizon = sensors.kinematicsHorizon;

                // Move the intercept to be at 0,0
                horizon[1] += (image.height() * 0.5) + horizon[0] * -(image.width() * 0.5);

                // This is our new images horizon
                classifiedImage->horizon = horizon;

                /**********************************************
                 *             FIND VISUAL HORIZON            *
                 **********************************************/

                std::vector<arma::uvec2> horizonPoints;

                // Cast lines to find our visual horizon
                for(uint i = 0; i < image.width(); i += m->VISUAL_HORIZON_SPACING) {

                    // Find our point to classify from (slightly above the horizon)
                    uint top = std::max(int(i * horizon[0] + horizon[1] - m->HORIZON_BUFFER), int(0));
                    top = std::min(top, image.height() - 1);
                    //uint top = std::min(uint(i * horizon[0] + horizon[1] + HORIZON_BUFFER), uint(image.height() - 1));

                    // Classify our segments
                    auto segments = m->quex.classify(image, lut, { i, top }, { i, image.height() - 1 });

                    // Our default green point is the bottom of the screen
                    arma::uvec2 greenPoint = { i, image.height() - 1 };

                    // Loop through our segments backward (top to bottom) to find our first green segment
                    for (auto it = segments.begin(); it != segments.end(); ++it) {

                        // If this a valid green point update our information
                        if(it->colour == ObjectClass::FIELD && it->length >= m->MINIMUM_VISUAL_HORIZON_SEGMENT_SIZE) {
                            greenPoint = it->start;
                            // We found our green
                            break;
                        }
                    }

                    horizonPoints.push_back(std::move(greenPoint));

                    insertSegments(*classifiedImage, segments, true);
                }

                // Do a convex hull on the map points to build the horizon
                for(auto a = horizonPoints.begin(); a < horizonPoints.end() - 2;) {

                    auto b = a + 1;
                    auto c = a + 2;

                    // Get the Z component of a cross product to check if it is concave
                    bool concave = 0 <   (double(a->at(0)) - double(b->at(0))) * (double(c->at(1)) - double(b->at(1)))
                                       - (double(a->at(1)) - double(b->at(1))) * (double(c->at(0)) - double(b->at(0)));

                    if(concave) {
                        horizonPoints.erase(b);
                        a = a == horizonPoints.begin() ? a : --a;
                    }
                    else {
                        ++a;
                    }
                }

                uint maxVisualHorizon = 0;
                uint minVisualHorizon = image.height();

                for(uint i = 0; i < horizonPoints.size() - 1; ++i) {
                    const auto& p1 = horizonPoints[i];
                    const auto& p2 = horizonPoints[i + 1];

                    maxVisualHorizon = std::max({ maxVisualHorizon, uint(p1[1]), uint(p2[1]) });
                    minVisualHorizon = std::min({ minVisualHorizon, uint(p1[1]), uint(p2[1]) });

                    double m = (double(p2[1]) - double(p1[1])) / (double(p2[0]) - double(p1[0]));
                    double b = - m * double(p1[0]) + double(p1[1]);

                    classifiedImage->visualHorizon.push_back({ double(p1[0]), m, b });
                }

                /**********************************************
                 *           CAST BALL FINDER LINES           *
                 **********************************************/

                /*
                    Here we cast lines to find balls.
                    To do this, we cast lines seperated so that any ball will have at least 2 lines
                    passing though it (possibly 3).
                    This means that lines get logrithmically less dense as we decend the image as a balls
                    apparent size will be larger.
                    These lines are cast from slightly above the visual horizon to a point where it is needed
                    (for the logrithmic grid)
                 */

                // Update equation
                /// gives between l and l+1 lines through ball
                ///
                /// l        = min lines through ball
                /// r        = radius of ball
                /// h        = robot's camera height
                /// p        = number of pixels below kinematics horizion
                /// $\alpha$ = pixels to tan(\theta) ratio
                ///
                /// $\Delta p=p^{2}\frac{2r\alpha}{lh}$

                // double screenBallSpacing = 0.5 / height;

                // auto lineL = classifiedImage->horizonPoints.begin();
                // auto lineR = classifiedImage->horizonPoints.end();

                // // Find the apex segment
                // for(auto it = visualHorizon.begin())

                // for(uint p = minVisualHorizon; p < image.height; p += p * p * screenBallSpacing) {

                //     // Test either end

                //     arma::vec2 start = { };
                //     arma::vec2 end = { };
                // }
                // Start at max green horizon
                // p += min(p2 * constant, 1);

                // Based on the horizon and level get an end point

                // line starts at visual horizon + buffer down to end point


                /**********************************************
                 *           CAST GOAL FINDER LINES           *
                 **********************************************/

                /*
                   Here we cast classification lines to attempt to locate the general area of the goals.
                   We cast lines only above the visual horizon (with some buffer) so that we do not over.
                   classify the mostly empty green below.
                 */

                // We start from either the lowest point on the green horizion, or the lowest goal transition
                int goalSearchStart = maxVisualHorizon;

                for(auto it = classifiedImage->verticalSegments.lower_bound(ObjectClass::GOAL);
                    it != classifiedImage->verticalSegments.upper_bound(ObjectClass::GOAL);
                    ++it) {

                    goalSearchStart = std::max(goalSearchStart, int(it->second.end[1]));

                }

                // Cast lines upward to find the goals
                for(int i = goalSearchStart; i >= 0; i -= m->GOAL_FINDER_LINE_SPACING) {

                    // Cast a full horizontal line here
                    auto segments = m->quex.classify(image, lut, { uint(0), uint(i) }, { image.width() - 1, uint(i) });
                    insertSegments(*classifiedImage, segments, false);
                }

                /**********************************************
                 *              CROSSHATCH BALLS              *
                 **********************************************/

                /*
                    This section improves the classification of the ball.
                    We first find all of the orange transitions that are below the visual horizon.
                    We then take the norm of these points to attempt to find a very rough "centre" for the ball.
                    Using the expected size of the ball at this position on the screen, we then crosshatch 2x the
                    size needed to ensure that the ball is totally covered.
                 */
                arma::running_stat_vec<arma::uvec> centre;
                for(auto it = classifiedImage->horizontalSegments.lower_bound(ObjectClass::BALL);
                    it != classifiedImage->horizontalSegments.upper_bound(ObjectClass::BALL);
                    ++it) {

                    auto& elem = it->second;

                    centre(elem.midpoint);


                    // Get the expected size of the ball at the
                }

                // Find the size of a ball at the position
                auto ballSize = centre.mean();

                // Cast lines in a radius around where the ball should be

                /**********************************************
                 *              CROSSHATCH GOALS              *
                 **********************************************/

                /*
                    Here we improve the classification of goals.
                    We do this by taking our course classification of the whole image
                    and generating new segments where yellow was detected.
                    We first generate segments above and below that are 2x the width of the segment
                    We then take these segments and generate segments that are 1.2x the width
                    This should allow a high level of detail without overclassifying the image
                 */

                for (uint i = 0; i < m->GOAL_FINDER_DETECTOR_LEVELS.size(); ++i) {

                    std::vector<ClassifiedImage<ObjectClass>::Segment> newSegments;

                    for(auto it = classifiedImage->horizontalSegments.lower_bound(ObjectClass::GOAL);
                        it != classifiedImage->horizontalSegments.upper_bound(ObjectClass::GOAL);
                        ++it) {

                        auto& elem = it->second;
                        arma::vec2 midpoint = arma::conv_to<arma::vec>::from(elem.midpoint);

                        arma::vec upperBegin = midpoint + arma::vec({ -double(elem.length) * m->GOAL_FINDER_DETECTOR_LEVELS[i],  double(m->GOAL_FINDER_LINE_SPACING) / std::pow(3, i + 1) });
                        arma::vec upperEnd   = midpoint + arma::vec({  double(elem.length) * m->GOAL_FINDER_DETECTOR_LEVELS[i],  double(m->GOAL_FINDER_LINE_SPACING) / std::pow(3, i + 1) });
                        arma::vec lowerBegin = midpoint + arma::vec({ -double(elem.length) * m->GOAL_FINDER_DETECTOR_LEVELS[i], -double(m->GOAL_FINDER_LINE_SPACING) / std::pow(3, i + 1) });
                        arma::vec lowerEnd   = midpoint + arma::vec({  double(elem.length) * m->GOAL_FINDER_DETECTOR_LEVELS[i], -double(m->GOAL_FINDER_LINE_SPACING) / std::pow(3, i + 1) });

                        upperBegin[0] = std::max(upperBegin[0], double(0));
                        upperBegin[0] = std::min(upperBegin[0], double(image.width() - 1));

                        upperEnd[0] = std::max(upperEnd[0], double(0));
                        upperEnd[0] = std::min(upperEnd[0], double(image.width() - 1));

                        lowerBegin[0] = std::max(lowerBegin[0], double(0));
                        lowerBegin[0] = std::min(lowerBegin[0], double(image.width() - 1));

                        lowerEnd[0] = std::max(lowerEnd[0], double(0));
                        lowerEnd[0] = std::min(lowerEnd[0], double(image.width() - 1));

                        // If the upper segment is valid
                        if(upperBegin[0] != upperEnd[0]
                          && (upperBegin[1] < image.height() && upperBegin[1] >= 0)
                          && (upperEnd[1] < image.height() && upperEnd[1] >= 0)) {

                            auto segments = m->quex.classify(image, lut, arma::conv_to<arma::uvec>::from(upperBegin), arma::conv_to<arma::uvec>::from(upperEnd));

                            newSegments.insert(newSegments.begin(), segments.begin(), segments.end());
                        }

                        // If the lower segment is valid and not the same as the upper segment
                        if(lowerBegin[0] != lowerEnd[0]
                          && (lowerBegin[1] < image.height() && lowerBegin[1] >= 0)
                          && (lowerEnd[1] < image.height() && lowerEnd[1] >= 0)) {

                            auto segments = m->quex.classify(image, lut, arma::conv_to<arma::uvec>::from(lowerBegin), arma::conv_to<arma::uvec>::from(lowerEnd));

                            newSegments.insert(newSegments.begin(), segments.begin(), segments.end());
                        }
                    }

                    insertSegments(*classifiedImage, newSegments, false);
                }

                emit(std::move(classifiedImage));
            });

        }

    }  // vision
}  // modules