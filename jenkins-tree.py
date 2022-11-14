#!/usr/bin/env python3

import jenkins
import json
import requests
import enum
import time
import serial
import struct


class NodeState(enum.IntEnum):
    OFFLINE = 0
    IDLE = enum.auto()
    BUSY = enum.auto()

class LedFX(enum.IntEnum):
    SOLID = 0
    SLOW_BLINK = enum.auto()
    FAST_BLINK = enum.auto()
    BREATHE = enum.auto()

def get_queue(server):
    qi = server.get_queue_info()
    test = []
    build = []
    for job in qi:
        why = job.get('why')
        if 'Waiting for next' in why:
            if 'test-ble' in why:
                test += [job]
            elif 'build-ncs' in why:
                build += [job]
    return {'queue': qi, 'test': test, 'build': build}

# queue = get_queue(server)
# print(f"Build queue: {len(queue['build'])}")
# print(f"Test  queue: {len(queue['test'])}")

def get_nodes_extrainfo(server, depth=0):
    """Low-level method to return the whole node info structure
    instead of just the name and status.
    """
    nodes_data = json.loads(server.jenkins_open(
        requests.Request('GET', server._build_url(jenkins.NODE_LIST, locals()))))
    return nodes_data["computer"]

def extract_info(name, info):
    idle = info['idle']
    offline = info['offline']
    if offline:
        status = NodeState.OFFLINE
    elif idle:
        status = NodeState.IDLE
    else:
        status = NodeState.BUSY
    return {'name': name, 'status': status}

def get_nodes(server):
    nodes_build = []
    nodes_test = []
    # st = time.time()
    nodes = get_nodes_extrainfo(server)
    # print(f'req time: {time.time() - st}')
    for node in nodes:
        try:
            name = node['displayName']
        except:
            continue
        if not (('build-' in name) or ('test-' in name)):
            continue
        info = node
        labels = info['assignedLabels']
        for label in labels:
            if 'build-ncs' in label['name']:
                nodes_build += [extract_info(name, info)]
            if 'node-test-ble' in label['name']:
                nodes_test += [extract_info(name, info)]
    return {'build': nodes_build, 'test': nodes_test}


# nodes = get_nodes(server)

def nodes_with_state(nodes, state: NodeState):
    return [node for node in nodes if node['status'] == state]

def build_report(name, nodes):
    busy = nodes_with_state(nodes, NodeState.BUSY)
    idle = nodes_with_state(nodes, NodeState.IDLE)
    offline = nodes_with_state(nodes, NodeState.OFFLINE)
    print(f'\n{name} nodes: ')
    print(f'busy: {len(busy)}')
    print(f'idle: {len(idle)}')
    print(f'offline: {len(offline)}')

# build_report("Build", nodes['build'])
# build_report("Test", nodes['test'])

C_RED = [0xff, 0x00, 0x00]
C_PURPLE = [0xc1, 0x2a, 0xb4]
C_YELLOW = [0xff, 0xd9, 0x00]
C_GREEN = [0x00, 0x20, 0x00]
# C_BLUE = [0x2c, 0xe8, 0x00]

def make_leds(color, effect, n):
    data = bytes(color) + struct.pack('B', effect)
    return data * n

def build_led_string(nodes, queue):
    busy_test = len(nodes_with_state(nodes['test'], NodeState.BUSY))
    idle_test = len(nodes_with_state(nodes['test'], NodeState.IDLE))
    wait_test = len(queue['test'])
    wait_build = len(queue['build'])

    print('busy test: {} idle test: {} wait test: {} wait build: {}'.format(
        busy_test, idle_test, wait_test, wait_build))

    return (make_leds(C_RED, LedFX.FAST_BLINK, busy_test)
            + make_leds(C_YELLOW, LedFX.FAST_BLINK, idle_test)
            + make_leds(C_PURPLE, LedFX.BREATHE, wait_test)
            + make_leds(C_GREEN, LedFX.SOLID, wait_build))

def send_data(data):
    # There's a bug here (or somewhere else): the tree doesn't light up all the
    # way to the top, it's missing a few leds
    if len(data) > (68 * 4):
        data = data[:(68*4) - 1]
    header = b'UART' + struct.pack('H', len(data)) + b'\0'
    # print(f'Header: {header.hex(" ")} Data: {data.hex(" ")}')
    port.write(header + data)



# Main refresh loop
while True:
    try:
        port = serial.Serial('/dev/serial/by-id/usb-SEGGER_J-Link_000682108520-if00', 115200, timeout=1)
        server = jenkins.Jenkins("https://my-jenkins-install.local/",
                                username="my-user",
                                password="my-token")

        while True:
            queue = get_queue(server)
            nodes = get_nodes(server)
            send_data(build_led_string(nodes, queue))
            time.sleep(5)
    except Exception as e:
        # Don't need to close the server?
        port.close()
        if e is KeyboardInterrupt:
            exit(0)
