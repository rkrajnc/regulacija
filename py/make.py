#!/usr/bin/python3

import os
import sys
import subprocess
import pickle
import urllib.request
from datetime import datetime

import objdump
import macrodump
import xml_parse
import tools
import packer

build_time = tools.mcutime().get()

defines = {
#  "F_CPU"   : 9216000,
#  "BAUD"    : 230400,
  "F_CPU"   : 16000000,
  "BAUD"    : 38400,
#  "NDEBUG" : "",
  "__ASSERT_USE_STDERR" : "", # I have my own assert right now anyway
  "__DELAY_BACKWARD_COMPATIBLE__" : "", # compile error with 4.7.2
  "PLAIN_CONSOLE" : 0,

  "BUILD_SEC"     : build_time[0],
  "BUILD_MIN"     : build_time[1],
  "BUILD_HOUR"    : build_time[2],
  "BUILD_WEEKDAY" : build_time[3],
  "BUILD_DAY"     : build_time[4],
  "BUILD_MONTH"   : build_time[5],
  "BUILD_YEAR"    : tools.to_int(build_time[6:8])
}

cflags = (
#"-g3",   # gstabs+
#"-ggdb3", # dwarf # segmentation fault in lto1
#"-feliminate-dwarf2-dups",
"-gdwarf-4", "-g3",
"-gstrict-dwarf",

"-Os",
#"-v", "-save-temps", # GCC debugging
#"-mmcu=atmega32",
"-mmcu=atmega328p",
"-std=gnu99",

#"-fstack-usage",

"-Wall",
"-Wextra",
"-pedantic",
#"-Wstack-usage",

"-mcall-prologues",
"-fshort-enums",
#"-combine",
"-fwhole-program",
"-fno-split-wide-types",
"-funsigned-char",
"-flto",

# throw out unneeded code - this seems to have no effect when -combine -fwhole-program is used
#"-fdata-sections",
#"-ffunction-sections",
#"-Wl,-gc-sections,-print-gc-sections",
#"-Wl,--relax", #TODO retry - that used to work but having segmentation faults now (8.12.2011); works with -gc-sections (27.6.2012)

# select printf
#"-Wl,-u,vfprintf", "-lprintf_min",
#"-Wl,-u,vfprintf", "-lprintf_flt",
)

cflags_fw = (
)
cflags_bootloader = (
"-mno-call-prologues",
#"-nostartfiles",
"-mno-interrupts",
#"-mtiny-stack",
)

#
# Includes
# 
includes = (
# STDLIB
"stdint.h",
"stdlib.h",
"stdio.h",
"string.h",
"stdbool.h",
"setjmp.h",

# AVR
"avr/io.h",
"avr/sleep.h",
"avr/interrupt.h",
"avr/wdt.h",
"avr/eeprom.h",
"avr/boot.h",
"avr/pgmspace.h",
"util/delay.h",
"util/atomic.h",
"util/crc16.h",

# custom
#"src/fifo.h",
)

sources_fw = (
"src/common.c",
"src/main.c",
"src/timer.c",
"src/cron.c",
"src/debug.c",
"src/sch.c",
"src/timer_q.c",
"src/clock.c",
"src/uart.c",
"src/crc8.c",
"src/onewire.c",
"src/ds18b20.c",
"src/relay.c",
"src/port.c",
"src/lcd.c",
"src/valve.c",
"src/loops.c",
"src/stack_check.c",
"src/watchdog.c",
"src/radiator.c",
"src/furnace.c",
"src/pumping.c",
"src/collector.c",
"src/console.c",
"src/print.c",
"src/exexec.c",
"src/config.c",
"src/flash.c",
"src/time.c",
"src/ac.c",
)

sources_bootloader = (
"src/common.c",
"src/bootloader.c",
)

def compile(sources, name):
  # Common part
  linker_script = "ld_" + name + ".x"
  base_cmd = [ "avr-gcc",
               "-T", linker_script,
               "-I", "src/auto",
             ]

  autoincludes = []
  for i in sources:
    if os.path.isfile(i[0:-1] + "h"): autoincludes[len(autoincludes):] = [i[0:-1] + "h"]

  for i in includes + tuple(autoincludes):
    base_cmd[len(base_cmd):] = [ "-include", i ]

  for d in defines:
    base_cmd[len(base_cmd):] = [ "-D" + d + "=" + str(defines[d]) ]
  base_cmd[len(base_cmd):] = cflags
  base_cmd[len(base_cmd):] = eval("cflags_" + name)
  
  # Compile
  objn = name + ".obj"
  c = subprocess.Popen(base_cmd + list(sources) + ["-o", objn])
  c.communicate()
  if c.returncode: return c.returncode
  
  ### Pre-linking stage
  #noln = "nol"
  #s = subprocess.Popen(base_cmd + list(sources) + ["-c", "-o", noln])
  #s.communicate()
  #if s.returncode: return s.returncode
  
  ## Pre-assembly stage
  #sn   = "ss.s"
  #s = subprocess.Popen(base_cmd + list(sources) + ["-S", "-o", sn])
  #s.communicate()
  #if s.returncode: return s.returncode

  # Defines
  deftmp = "deftmp.c"
  cc = open(deftmp, "w")
  subprocess.Popen(["cat"] + list(sources), stdout = cc)
  cc.close()
  d = subprocess.Popen(base_cmd + ["-dM", "-E", deftmp, "-o", deftmp])
  d.communicate()
  if d.returncode: return d.returncode
  global macros
  macros = macrodump.macrodump(deftmp)
  os.remove(deftmp)

  #subprocess.Popen(["avr-objdump", "-g", name + ".obj",], stdout = open("test_c_dump.c", 'w')).communicate()
  subprocess.Popen(["avr-size", "-A", name + ".obj"]).communicate()

  # obj -> dbg
  #binn = name + ".dbg"
  #b = subprocess.Popen(["avr-objcopy", "--only-keep-debug", objn, binn]) 
  #b.communicate()
  #b = subprocess.Popen(["avr-objcopy", "-j", ".stab", "-j", ".stabstr", objn, "fw.stab"]).communicate() 

  # obj -> bin
  binn = name + ".bin"
  b = subprocess.Popen(["avr-objcopy", "-R", ".eeprom", "-O", "binary", objn, binn]) 
  b.communicate()
  return b.returncode



recompile = {
  'fw'         : True,
  'bootloader' : True,
}

if len(sys.argv) > 1:
  for k in recompile:
    if k not in sys.argv[1:]: recompile[k] = False

cmptime = (lambda: 0, lambda: os.stat(".timestamp").st_mtime)[os.path.exists(".timestamp")]()

if cmptime:
  newer = set()
  for w in os.walk("."):
    for f in w[2]: newer.add(w[0] + "/" + f)
  newer = filter(lambda x: os.stat(x).st_mtime > cmptime, newer)
  newer = filter(lambda x: x[0:7] != "./.git/" , newer)
  newer = filter(lambda x: x      != "./meta"  , newer)
  newer = filter(lambda x: x[-4:] != ".pyc"    , newer)
  newer = filter(lambda x: x[-4:] != ".swp"    , newer)
  newer = filter(lambda x: x[-8:] != ".tar.bz2", newer)
  newer = set(newer)
  if (newer):
    print("Modified files:")
    for f in newer: print(f)
  newer = filter(lambda x: x != "./py/server.py"    , newer)
  newer = filter(lambda x: x != "./py/gum.py"       , newer)
  newer = filter(lambda x: x != "./py/uart.py"      , newer)
  newer = filter(lambda x: x != "./py/html_tools.py", newer)
  newer = set(newer)
  
  if newer:
    only_bootloader  = set(sources_bootloader) - set(sources_fw)
    only_fw          = set(sources_fw) - set(sources_bootloader)
    only_bootloader  = set(map(lambda x: './' + x, only_bootloader))
    only_fw          = set(map(lambda x: './' + x, only_fw        ))
    newer_fw         = set(filter(lambda x: x not in only_bootloader, newer))
    newer_bootloader = set(filter(lambda x: x not in only_fw        , newer))
    only_fw          = filter(lambda x: x[-2:]  == '.c', only_fw        )
    only_bootloader  = filter(lambda x: x[-2:]  == '.c', only_bootloader)
    only_fw          =    map(lambda x: x[0:-1] + 'h',   only_fw        )
    only_bootloader  =    map(lambda x: x[0:-1] + 'h',   only_bootloader)
    newer_fw         = set(filter(lambda x: x not in only_bootloader, newer_fw        ))
    newer_bootloader = set(filter(lambda x: x not in only_fw        , newer_bootloader))
    if not newer_fw        : recompile['fw']         = False
    if not newer_bootloader: recompile['bootloader'] = False
  else:
    for i in recompile: recompile[i] = False
  
  for i in recompile:
    if not recompile[i]: print("Not recompiling", i)

if any(recompile.values()):
  # Generate additional sources from XML files
  xml_parse.gen_c()
  
  for i in recompile:
    if recompile[i] and compile(eval('sources_' + i), i): print("Compile error"); exit()

  obj_sym = objdump.correct_symbols(objdump.get_symbols(objs = [ "fw.obj" ]))
  reg_sym = macros.getregs()
  if not set(reg_sym.keys()).isdisjoint(set(obj_sym.keys())): raise
  symbols = dict()
  symbols.update(obj_sym)
  symbols.update(reg_sym)
  print("_etext:", symbols['_etext']['adr'])
  
  meta = open("meta", 'wb')
  pickle.dump({ 'macros'  : macros.getdict(),
                'symbols' : symbols,
              }, meta)
  meta.close()

packer.pack(cmptime)

#TODO use mechanize to send it ???

#encodedstring = base64.encodebytes(bytes(b"servis:t0r0nt0"))[:-1]
#auth = "Basic %s" % encodedstring
#
#
#auth_handler = urllib.request.HTTPBasicAuthHandler()
##auth_handler.add_password(realm=None, uri='http://stefuc.homeip.net:8000/update_upload', user='servis', passwd='t0r0nt0')
#auth_handler.add_password(realm=None, uri='http://stefuc.homeip.net:8000/update_upload', user=None, passwd={"Authorization": auth })
#opener = urllib.request.build_opener(auth_handler)
#urllib.request.install_opener(opener)
#
#req = urllib.request.Request("http://stefuc.homeip.net:8000/update")
#req.add_header("Authorization", auth)
#
#urllib.request.urlopen(req) 

#import poster
#poster.streaminghttp.register_openers()
#datagen, headers = poster.encode.multipart_encode({ 'update' : open("update.tar.bz2") })
#
#req = urllib.request.Request("http://stefuc.homeip.net:8000/flash", datagen, headers)
#res = urllib.request.urlopen(req)
#print(res.read())

open(".timestamp", 'w')
