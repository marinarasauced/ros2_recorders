from typing import Any

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def launch_setup(context):
    # The config file is loaded first. Arguments that are not empty replace its values.
    parameters: list[Any] = [LaunchConfiguration('config').perform(context)]

    overrides: dict[str, Any] = {}
    for name in ('directory', 'trial'):
        value = LaunchConfiguration(name).perform(context).strip()
        if value:
            overrides[name] = value

    topics = LaunchConfiguration('topics').perform(context).strip()
    if topics:
        overrides['topics'] = [topics]      # "a, b, c" is split by the node

    if overrides:
        parameters.append(overrides)

    return [Node(
        package='recorders',
        executable='recorder',
        namespace=LaunchConfiguration('namespace'),
        output='screen',
        parameters=parameters,
    )]


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument(
            'config',
            default_value=PathJoinSubstitution([FindPackageShare('recorders'), 'config', 'recorder.yaml']),
            description='Parameter file. Copy config/recorder.yaml and edit it.'
        ),
        DeclareLaunchArgument('namespace', default_value='', description='Namespace of the node. Relative topics use it.'),
        DeclareLaunchArgument('directory', default_value='', description='Replaces directory from the config file.'),
        DeclareLaunchArgument('topics', default_value='', description='Comma separated topics. Replaces topics from the config file.'),
        DeclareLaunchArgument('trial', default_value='', description="Replaces trial from the config file. 'auto' or a name."),
        OpaqueFunction(function=launch_setup),
    ])
