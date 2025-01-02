#!/bin/bash

cc clinetdemo.c -o clinetdemo -lm -O0 -ggdb -fsanitize=address,leak