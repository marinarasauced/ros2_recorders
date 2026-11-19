# recorders

![Ubuntu](https://img.shields.io/badge/Ubuntu-26.04-E95420?logo=ubuntu&logoColor=white)
![ROS 2](https://img.shields.io/badge/ROS_2-Lyrical-22314E?logo=ros&logoColor=white)
![C++](https://img.shields.io/badge/C++-20-00599C?logo=cplusplus&logoColor=white)
![License](https://img.shields.io/badge/License-MIT-yellow)

ROS 2 package that records topics directly to `.csv` files ; one node reads the message layout at run time, so any message type works without per-type code. Not recommended for large arrays (images, point clouds) or high data rates ; use [rosbag2](https://github.com/ros2/rosbag2) instead.

## License

This package is released under an [MIT license](https://github.com/marinarasauced/ros2_logger_csv/blob/main/LICENSE).

Authors: [Marina Nelson](https://github.com/marinarasauced) <br/>
Maintainer: Marina Nelson, marinarasauced@outlook.com

Parts of this package were rewritten with the assistance of Claude Sonnet 5.5, an AI model by Anthropic.
All AI contributions were reviewed, tested, and applied by the author.

## Contents

- [Requirements](#requirements)
- [Installation](#installation)
- [Usage](#usage)
- [Parameters](#parameters)
- [Log files](#log-files)

## Requirements

| Component | Version tested |
|---|---|
| Ubuntu | 26.04 |
| ROS 2 | Lyrical |
| C++ | 20 |

The ROS dependencies are installed with rosdep.

## Installation

Clone the package into your workspace.

```zsh
cd ~/ros2_ws/src
git clone https://github.com/marinarasauced/ros2_logger_csv
```

Install all ROS dependencies.

```zsh
cd ~/ros2_ws
rosdep install --from-paths src --ignore-src -r -y
```

Build the package.

```zsh
colcon build --packages-select recorders
source install/setup.zsh
```

## Usage

Record two topics to `~/logs`.

```zsh
ros2 run recorders recorder --ros-args -p directory:=~/logs -p 'topics:=[topic1, /ns/topic2]'
```

Relative topic names use the namespace of the node.

```zsh
ros2 run recorders recorder --ros-args -r __ns:=/robot1 -p directory:=~/logs -p 'topics:=[topic1, topic2]'
```

Start with the launch file and the parameter file.
Copy `config/recorder.yaml`, edit it, and pass it as `config`.

```zsh
ros2 launch recorders recorder.launch.py config:=/path/to/your/recorder.yaml
```

The launch file also accepts `namespace`, `directory`, `topics` and `trial`.
A value that is not empty replaces the same value from the parameter file.

```zsh
ros2 launch recorders recorder.launch.py namespace:=robot1 directory:=~/logs topics:='topic1, topic2' trial:=auto
```

The node waits for topics that are not advertised yet, and stops waiting after `discovery_timeout`.
It matches the QoS of the publishers : best effort if any publisher uses it, transient local if all publishers do.
If a publisher with a stricter QoS appears later, the node subscribes again.

## Parameters

All parameters are read when the node starts, and cannot be changed while it runs.

| Parameter | Default | Contents |
|---|---|---|
| `directory` | required | directory for the log files, `~` is expanded |
| `trial` | `""` | sub-directory for the log files : empty for none, `auto` for the next `NNNN_YYYY-MM-DD_HH-MM-SS`, other for a fixed name |
| `topics` | required | topics to record, relative names use the node namespace |
| `discovery_period` | `5.0` | seconds between checks for topics that are not advertised yet |
| `discovery_timeout` | `60.0` | seconds to wait for topics before giving up, `0` waits forever |
| `flush_interval` | `1.0` | seconds between file flushes |
| `queue_depth` | `10` | subscription queue depth |
| `overwrite` | `false` | replace existing files, otherwise an existing file is an error |

## Log files

Each topic has one file named after the topic : `/ns/topic` is written to `ns__topic.csv`.
The first row holds the column names, and each message adds one row.
The first column, `received_time_ns`, is the time the message was received in nanoseconds since the epoch.
The other columns hold the fields of the message, with names that start with `msg.`.

Example for the topic `/test_topic` of type `std_msgs/msg/Header`:

```
received_time_ns,msg.stamp.sec,msg.stamp.nanosec,msg.frame_id
1795084149650808678,0,0,
1795084149851017122,0,0,
```

- Values are separated by commas. A value that contains a comma, a quote or a new line is quoted.
- An array is one value: `"[1, 2, 3]"`. A message inside an array is written as `{a, b}`.
- Booleans are `true` or `false`.
