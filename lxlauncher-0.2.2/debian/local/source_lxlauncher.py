"""
Copyright (c) 2010 Julien Lavergne <gilir@ubuntu.com>
Copyright (c) 2011 Matthew Byers <stlsaint@ubuntu.com>
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
 
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software Foundation,
  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
"""

import os
import apport.hookutils

#Detect session
session = os.environ['DESKTOP_SESSION']

#If it's not a specific session, fallback to LXDE
if not session:
    session = "LXDE"

#Set location of various configuration files
system_conf = "/etc/xdg/lxlauncher/"
home_conf = os.path.expanduser("~/.config/lxlauncher/")

#Set description for each file reported by apport
report_config_system = "Config_System_" + session
report_config_home = "Config_Home_" + session

def add_info(report):
    li = ['settings.conf','gtkrc']
    for conf in li:
        # If a config file exist in HOME, report it instead of the system one.
        if os.path.exists(os.path.join(home_conf,conf)):
            report_config_home_conf = report_config_home + "_" + conf
            report[report_config_home_conf] = apport.hookutils.read_file(os.path.join(home_conf,conf))
        else:
            report_config_system_conf = report_config_system + "_" + conf
            report[report_config_system_conf] = apport.hookutils.read_file(os.path.join(system_conf,conf))
