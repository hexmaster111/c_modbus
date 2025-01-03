#!/bin/bash

cc clinetdemo.c modbus.c -o clinetdemo -lm -O0 -ggdb -fsanitize=address,leak