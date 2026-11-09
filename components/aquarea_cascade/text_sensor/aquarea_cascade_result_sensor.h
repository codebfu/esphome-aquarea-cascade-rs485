#pragma once

#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

namespace esphome {
namespace aquarea_cascade {

// Marker type so the text_sensor platform has a dedicated class id.
class AquareaCascadeResultSensor : public text_sensor::TextSensor, public Component {};

}  // namespace aquarea_cascade
}  // namespace esphome
