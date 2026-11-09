#pragma once

#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

namespace esphome {
namespace aquarea_cascade {

class AquareaCascadeTextSensor : public text_sensor::TextSensor, public Component {};

}  // namespace aquarea_cascade
}  // namespace esphome
