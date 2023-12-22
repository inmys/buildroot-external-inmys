#!/bin/sh

# set all passwords time limit to infinity
sed -i -e 's/:::::::/::0:::::/' ${1}/etc/shadow
