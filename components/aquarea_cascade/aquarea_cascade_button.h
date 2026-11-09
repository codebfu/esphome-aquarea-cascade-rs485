#pragma once

#include "esphome/components/button/button.h"
#include "esphome/core/component.h"
#include "aquarea_cascade.h"

namespace esphome {
namespace aquarea_cascade {

class AquareaCascadeButton : public button::Button, public Component {
 public:
  void set_parent(AquareaCascade *parent) { this->parent_ = parent; }
  void set_command(Command command) { this->command_ = command; }

 protected:
  void press_action() override {
    if (this->parent_ != nullptr) {
      this->parent_->request_command(this->command_);
    }
  }

  AquareaCascade *parent_{nullptr};
  Command command_{Command::OFF};
};

}  // namespace aquarea_cascade
}  // namespace esphome
