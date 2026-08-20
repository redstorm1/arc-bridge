from __future__ import annotations

import os
import subprocess
import tempfile
from pathlib import Path

from run_delivery_test import find_compiler, find_std_flag


STUBS = {
    "esphome/core/component.h": r"""
#pragma once
namespace esphome {
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  void disable_loop() {}
  void status_set_warning() {}
  void status_clear_warning() {}
};
}  // namespace esphome
""",
    "esphome/core/log.h": r"""
#pragma once
#define ESP_LOGD(...)
#define ESP_LOGV(...)
#define ESP_LOGW(...)
#define ESP_LOGCONFIG(...)
""",
    "esphome/components/cover/cover.h": r"""
#pragma once
#include <functional>
#include <utility>
#include <vector>

namespace esphome {
namespace cover {
enum CoverOperation { COVER_OPERATION_IDLE, COVER_OPERATION_OPENING, COVER_OPERATION_CLOSING };

class CoverTraits {
 public:
  void set_is_assumed_state(bool) {}
  void set_supports_position(bool) {}
  void set_supports_stop(bool) {}
};

class Cover;
class OptionalFloat {
 public:
  bool has_value() const { return has_value_; }
  float operator*() const { return value_; }
  OptionalFloat &operator=(float value) {
    value_ = value;
    has_value_ = true;
    return *this;
  }

 private:
  bool has_value_{false};
  float value_{0.0f};
};

class CoverCall {
 public:
  explicit CoverCall(Cover *parent) : parent_(parent) {}
  bool get_stop() const { return stop_; }
  const OptionalFloat &get_position() const { return position_; }
  void set_stop(bool stop) { stop_ = stop; }
  void set_position(float position) { position_ = position; }
  void perform();

 private:
  Cover *parent_;
  bool stop_{false};
  OptionalFloat position_;
};

class Cover {
 public:
  virtual ~Cover() = default;
  float position{1.0f};
  CoverOperation current_operation{COVER_OPERATION_IDLE};
  virtual CoverTraits get_traits() = 0;
  CoverCall make_call() { return CoverCall(this); }
  template<typename F> void add_on_state_callback(F &&callback) {
    callbacks_.emplace_back(std::forward<F>(callback));
  }
  void publish_state(bool = true) {
    for (auto &callback : callbacks_) callback();
  }
  bool has_state() const { return has_state_; }
  void set_has_state(bool has_state) { has_state_ = has_state; }

 protected:
  friend class CoverCall;
  virtual void control(const CoverCall &call) = 0;

 private:
  bool has_state_{false};
  std::vector<std::function<void()>> callbacks_;
};

inline void CoverCall::perform() { parent_->control(*this); }
}  // namespace cover
}  // namespace esphome

#define LOG_COVER(...)
""",
    "esphome/components/uart/uart.h": r"""
#pragma once
namespace esphome { namespace uart { class UARTDevice {}; } }
""",
    "esphome/components/sensor/sensor.h": r"""
#pragma once
namespace esphome { namespace sensor { class Sensor {}; } }
""",
    "esphome/components/text_sensor/text_sensor.h": r"""
#pragma once
namespace esphome { namespace text_sensor { class TextSensor {}; } }
""",
}


def write_stubs(root: Path) -> None:
    for relative_path, source in STUBS.items():
        path = root / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source.lstrip(), encoding="utf-8")


def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    compiler = find_compiler()
    std_flag = find_std_flag(compiler, repo_root)

    sources = [
        repo_root / "tests" / "cover_state_test.cpp",
        repo_root / "esphome" / "components" / "arc_bridge" / "arc_cover.cpp",
        repo_root / "esphome" / "components" / "arc_bridge_group" / "arc_bridge_group_cover.cpp",
    ]

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_root = Path(tmpdir)
        write_stubs(tmp_root)
        binary = tmp_root / ("cover_state_test.exe" if os.name == "nt" else "cover_state_test")
        cmd = [
            compiler,
            std_flag,
            "-Wall",
            "-Wextra",
            "-pedantic",
            "-ffunction-sections",
            "-fdata-sections",
            *(str(source) for source in sources),
            "-I",
            str(tmp_root),
            "-I",
            str(repo_root),
            "-Wl,--gc-sections",
            "-o",
            str(binary),
        ]
        subprocess.run(cmd, check=True, cwd=repo_root)
        subprocess.run([str(binary)], check=True, cwd=repo_root)


if __name__ == "__main__":
    main()
