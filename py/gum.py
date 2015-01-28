#!/usr/bin/python3

import os, sys
import time
import pickle
import threading
import xml.etree.ElementTree as etree

import uart
import tools
import packer

class Gum():
  icnt = 0
  icnt_lock = threading.Lock()

  def __init__(self, port = None, rate = None, meta = None, sw = None):
    with Gum.icnt_lock:
      if not Gum.icnt:
        self.connect(port = port, rate = rate, meta = meta, sw = sw)
      Gum.icnt += 1

  # TODO: do the opposite way around - read parameters from avalible files
  def connect(self, port = None, rate = None, meta = None, sw = None):
    try:
      unpickled = pickle.load(open("gum.cache", 'rb'))
    except IOError:
      pass
    except EOFError:
      pass
    else:
      #if not port: port = unpickled['port']
      if not rate: rate = unpickled['rate']
      if not meta: meta = unpickled['meta']

    if port == None:
      ports = ( '/dev/' + p for p in os.listdir('/dev/') if p.startswith('ttyUSB') or p.startswith('ttyS'))
    else:
      if type(port) == str:
        ports = ( port, )
      else:
        try:
          ports = iter(port)
        except TypeError:
          ports = ( port, )
    if not ports: raise Exception("No port found!")
    ports = tuple(ports)
    
    rates = ( 38400, 230400, 1000000, 115200, 9600 )
    #rates = rates + tuple(reversed(sorted(tuple(set(uart.uart.BAUDRATES) - set(rates)))))
    if rate:
      rates = filter(lambda x: x != rate, rates)
      rates = tuple([rate]) + tuple(rates)
    
    try:
      for uart.uart.port in ports:
        build = False
        try:
          for uart.uart.baudrate in rates:
            uart.uart.open()
            try:
              build = uart.access(0, 0x100, bytearray(8))
              break;
            except uart.ProtocolErr as inst:
              print("Failed:", uart.uart.port, uart.uart.baudrate, "reason:", inst)
              uart.uart.close()
              if uart.uart.baudrate == rates[-1]: raise
              else:                               continue
        except uart.ProtocolErr:
          if uart.uart.port == ports[-1]: raise
          else:                           continue
        if build: break
    except uart.ProtocolErr:
      print("Failed to autoconnect, defaulting to the first listed ...")
      uart.uart.port     = ports[0]
      uart.uart.baudrate = rates[0]
 
    #
    # Meta
    #
    def extract_build(meta):
      return bytearray([
        meta['macros']["BUILD_SEC"    ],
        meta['macros']["BUILD_MIN"    ],
        meta['macros']["BUILD_HOUR"   ],
        meta['macros']["BUILD_WEEKDAY"],
        meta['macros']["BUILD_DAY"    ],
        meta['macros']["BUILD_MONTH"  ]] +
        list(tools.to_bytes(meta['macros']["BUILD_YEAR"   ], 2)))
 
    sws = filter(lambda x: x[0:6] == "update" and x[-8:] == ".tar.bz2", os.listdir("."))
    sws = sorted(sws, key=lambda x: os.stat(x).st_mtime)
    sws = list(reversed(sws))
    if sw:
      sws = filter(lambda x: x == sw, sws)
      sws = tuple([sw]) + tuple(sws)
    
    def sw2meta(sw):
      pickled_meta = packer.getfile(sw, 'meta')
      if pickled_meta: return pickle.load(pickled_meta)
      else           : return None

    meta_latest  = None
    build_latest = bytearray(8)
    metafs = tuple([lambda: meta]) + tuple(map(lambda x: lambda: sw2meta(x), sws))
    for metaf in metafs:
      meta_new = metaf()
      if meta_new:
        build_curr = extract_build(meta_new)
        if meta_new and build_curr == build:
          break
        elif bytearray(reversed(build_curr)) > bytearray(reversed(build_latest)):
          meta_latest  = meta_new
          build_latest = build_curr
      if metaf == metafs[-1]:
        if not meta_latest: raise Exception("No meta found")
        print("Failed to find appropriate meta (", tools.mcutime(build), "). Failing back to latest found (", tools.mcutime(build_latest), ").")
        meta_new  = meta_latest
        build = build_latest
    Gum.meta = meta_new
    uart.max_write_len = Gum.meta['symbols']['rx_buf']['size']

    print("Connected to", uart.uart.port, " rate", uart.uart.baudrate, tools.mcutime(build))
    if port and port != uart.uart.port:     print("Port changed (", port                              , "->", uart.uart.port                        , ")")
    if rate and rate != uart.uart.baudrate: print("Rate changed (", rate                              , "->", uart.uart.baudrate                    , ")")
    if meta and meta != Gum.meta:           print("Meta changed (", tools.mcutime(extract_build(meta)), "->", tools.mcutime(extract_build(Gum.meta)), ")")

  def __del__(self):
    with Gum.icnt_lock:
      if hasattr(self, 'meta'):
        Gum.icnt -= 1
        if not Gum.icnt:
          to_pickle = { 'port' : uart.uart.port,
                        'rate' : uart.uart.baudrate,
                        'meta' : Gum.meta,
                      }
          pickle.dump(to_pickle, open("gum.cache", 'wb'))
          uart.uart.close()
          print("Disconnected.")

  #
  # R/W functions
  #

  def read_symbol(self, name, shape=int):
    if Gum.meta['symbols'][name]['mem'] == 'flash':
      arr = self.read_flash(Gum.meta['symbols'][name]['adr'], bytearray(Gum.meta['symbols'][name]['size']))
    else:
      arr = uart.access(0, Gum.meta['symbols'][name]['adr'], bytearray(Gum.meta['symbols'][name]['size']))
  
    if shape == int:
      return tools.to_int(arr)
    else:
      return arr
  
  def read_symbol_dbgcp(self, name, shape=int):
    if Gum.meta['symbols'][name]['mem'] == 'ram' and \
       Gum.meta['symbols']['__dbg2cp_start']['adr'] <= Gum.meta['symbols'][name]['adr'] < Gum.meta['symbols']['__dbg2cp_end']['adr']:
      offset = Gum.meta['symbols']['__dbgcp_start']['adr'] - Gum.meta['symbols']['__dbg2cp_start']['adr']
      arr = uart.access(0, Gum.meta['symbols'][name]['adr'] + offset, bytearray(Gum.meta['symbols'][name]['size']))
    else:
      raise Exception("Not a dbgcp symbol.")
  
    if shape == int:
      return tools.to_int(arr)
    else:
      return arr
  
  def write_symbol(self, name, dat):
    if (type(dat) == int): dat = tools.to_bytes(dat, Gum.meta['symbols'][name]['size'])
    if Gum.meta['symbols'][name]['size'] < len(dat): raise Exception("Write over the symbol length")
    if Gum.meta['symbols'][name]['mem'] == 'flash':
      self.write_flash(Gum.meta['symbols'][name]['adr'], dat)
    else:
      uart.access(1, Gum.meta['symbols'][name]['adr'], dat)
  
  def read_flash(self, adr, dat):
    length = min(len(dat[0:0xff]), Gum.meta['symbols']['flash_buf']['size'])
    self.exexec('flash_read', [ Gum.meta['symbols']['flash_buf']['adr'], adr, length ])
    arr = uart.access(0, Gum.meta['symbols']['flash_buf']['adr'], dat[0:length])
    if len(dat) > length:
      arr.extend(self.rite_flash(adr + length, dat[length:]))
    return arr
  
  def write_flash(self, adr, dat):
    length = min(len(dat[0:0xff]), Gum.meta['symbols']['flash_buf']['size'])
    arr = uart.access(1, Gum.meta['symbols']['flash_buf']['adr'], dat[0:length])
    self.exexec('flash_write', [ Gum.meta['symbols']['flash_buf']['adr'], adr, length ])
    if len(dat) > length:
      arr.extend(self.write_flash(adr + length, dat[length:]))
    return arr
  
  
  #
  # Flashing stuff
  #
  
  def flash_fw(self, f):
    print("Saving config ... ")
    #conf = self.get_config()
    print("done.")
    
    while 1:
      with Gum.icnt_lock:
        if Gum.icnt <= 1:
          try:
            print("Executing bootloader ... ")
            self.exexec(Gum.meta['symbols']['__bootloader_adr']['adr'], block = False)
            print("done.")
          except uart.ProtocolErr as inst:
            print("Error (", inst, "). Maybe already running.")
            uart.reset()
          
          print("Waiting for initial ACK ...")
          if uart.get(timeout = 10.0) != 0xa5: raise Exception("Failed to start bootloader")
          print("got!")
          
          print("Bootloader started! Flashing...")
          
          start = time.clock()
   
          binary = bytearray(f.read())
          binary.reverse()
          size = len(binary)
          print("Size:", size)
          if not (0 < size <= 32 * 1024): raise Exception("File size error")
   
          size_pac = tools.to_bytes(size, 2)
          size_pac.reverse()
          uart.flash_put(size_pac)
          
          length = size % Gum.meta['macros']['SPM_PAGESIZE']
          if length == 0: length = Gum.meta['macros']['SPM_PAGESIZE']
   
          cnt = 0;
          while cnt < size:
            pac = binary[cnt:cnt+length] + bytearray([0xa5])
            uart.flash_put(pac)
            cnt += length
            length = Gum.meta['macros']['SPM_PAGESIZE']
   
          print("Time elapsed:", time.clock() - start)
          
          if uart.get() == 0xa5: print("Succeed!")
          else:         raise Exception("Failed!")

          #time.sleep(5.0) # TODO uC reply with build instead of waiting? What about BAUD?
          print("Reconnecting...")
          self.connect()
          break

    print("Restoring config ... ")
    #self.set_config(conf)
    print("done.")

    print("Setting time ... ")
    self.set_time()
    print("done.")
  
  def flash_bootloader(self, f):
    print("Flashing bootloader...")
    binary = bytearray(f.read())
    print("Size:", len(binary))
    for i in range(0, len(binary), Gum.meta['macros']['SPM_PAGESIZE']):
      page = binary[i:i+Gum.meta['macros']['SPM_PAGESIZE']]
      self.write_symbol('flash_buf', page)
      self.exexec('flash_write_block', [ Gum.meta['symbols']['flash_buf']['adr'], Gum.meta['symbols']['__bootloader_adr']['adr'] + i, len(page) ])
    print("Done!")
  
  
  #
  # Text console
  #
  
  def readcon(self, max_read=1.0):
    if 'print_buf' in Gum.meta['symbols']:
      buf  = Gum.meta['symbols']["print_buf"]['adr']
      size = Gum.meta['symbols']["print_buf"]['size']
      nr = int(max_read * size)
      wp = self.read_symbol("print_buf_wp")
      rp = self.read_symbol("print_buf_rp")

      if 0 <= wp < size and 0 <= rp < size:
        ovf = 0
  
        data = bytearray()
        if   wp > rp:
          nr = min(nr, wp - rp)
          data += uart.access(0, buf + rp, bytearray(nr))
        elif wp < rp:
          top    = min(nr, size - rp)
          bottom = min(nr - top, wp)
          nr = top + bottom
          data += uart.access(0, buf + rp, bytearray(top))
          if bottom > 0:
            data[len(data):] += uart.access(0, buf, bytearray(bottom))
        else:
          nr = 0
  
        if nr:
          rp += nr
          if rp >= size: rp -= size
          self.write_symbol("print_buf_rp", rp)
   
        ovf = self.read_symbol("print_buf_ovf")
        wp  = self.read_symbol("print_buf_wp")
        
        ret_val = data.decode('utf8', 'ignore')
  
        if wp == rp and ovf != 0:
          self.write_symbol("print_buf_ovf", 0)
          ret_val += "\n\n### LOST >=" + str(ovf) + "CHARACTERS ###\n\n"
      else:
        ovf = self.read_symbol("print_buf_ovf")
        ret_val = "\n\n### CONSOLE CORRUPTED (wp: " + str(wp) + " rp: " + str(rp) + " ovf: " + str(ovf) + "). Clearing the mess. ###\n\n"
        self.write_symbol("print_buf_rp" , 0)
        self.write_symbol("print_buf_wp" , size - 1)
        self.write_symbol("print_buf_ovf", 0)
        self.write_symbol("print_buf_wp" , 0)
    
    else: ret_val = ""
  
    return ret_val
  
  
  #
  # External executor
  #
  
  def exexec(self, func, arg = [0, 0, 0, 0], block = True):
    max_args = 4
    
    if len(arg) > max_args:
      raise Exception("To many arguments to exexec!")
    else:
      for i in range(max_args):
        if i < len(arg):
          if type(arg[i]) != int:
            if arg[i]: arg[i] = int(arg[i], 0)
            else:      arg[i] = 0
        else:
          arg.append(0)
  
    arg_buf = bytearray()
    for i in arg:
      arg_buf.insert(0, (i >> 8) & 0xff)
      arg_buf.insert(0, (i >> 0) & 0xff)
  
    with self.exexec.lock:
      while self.read_symbol("exexec_func") != 0:
        print("exexec busy with", hex(self.read_symbol("exexec_func")))
        time.sleep(1.0)
  
      self.write_symbol("exexec_buf", arg_buf)
      if type(func) == int: adr = func
      else                : adr = Gum.meta['symbols'][func]['adr']
      adr >>= 1
      
      with uart.access.lock:
        self.write_symbol("exexec_func", adr)
        
        if block:
          expected = ( 1, Gum.meta['symbols']['exexec_func']['adr'], bytearray([0, 0]) )
          timeout = 15
          check   = 3
          try:
            response = uart.receive(timeout = check)
            if response != expected:
              raise Exception("Exexec failed (wrong packet received: expected %s, got %s)." % (str(expected), str(response)))
          except uart.RxErrTimeout:
            val = self.read_symbol("exexec_func")
            if   val == adr:
              print("Busy running", func, "...")
              timeout -= check
            elif val == 0:
              print("Exexec finished without response?")
            else:
              raise
  
      if block:
        return_bytes = self.read_symbol("exexec_buf", None)
        ret_val = []
        for i in range(0,len(return_bytes),2):
          ret_val.insert(0, return_bytes[i] + return_bytes[i+1] * 256)
        return ret_val
  exexec.lock = threading.Lock()
  
  
  
  #def ds18b20_get_temp(self, sensor_list = None, resolution = 0, rty = 1):
  #  if sensor_list == None:
  #    sensor_list = etree.parse("xml/xml.xml").getroot().find("ds18b20_list").findall("ds18b20")
  #
  #  arr = bytearray(len(sensor_list) * 2)
  #  for i in range(len(sensor_list)):
  #    arr[i * 2] = i
  #
  #  uart.access(1, Gum.meta['symbols']['_end']['adr'], arr)
  #  self.exexec("ds18b20_get_temp_tab", [ len(sensor_list), resolution, rty, Gum.meta['symbols']['_end']['adr']])
  #  arr = uart.access(0, Gum.meta['symbols']['_end']['adr'], arr)
  # 
  #  for i in range(len(sensor_list)):
  #    v = tools.to_int(arr[2*i:2*i+2])
  #    if v >= 0x8000: v -= 0x10000
  #    v /= 256.0
  #    sensor_list[i] = v
  #  return sensor_list
  
  def ds18b20_get_temp(self, sensor_list = None, resolution = 0, rty = 1):
    if sensor_list == None:
      sensor_list = [ i for i in range(self.read_symbol('ds18b20_nr')) ]
  
    for i in sensor_list:
      v = self.exexec("ds18b20_get_temp", [ i, resolution, rty])[0]
      if v >= 0x8000: v -= 0x10000
      v /= 256.0
      sensor_list[i] = v
    return sensor_list
  
  
  
  def valve_state(self, i):
    return self.exexec("valve_get", [ i ])[0]
  
  def relay_get(self, i):
    return self.exexec("relay_get", [ i ])[0] & 0xff
  
  
  
  def set_time(self, t = None, format = None):
    if t == None: t = time.localtime()
    self.write_symbol('date', tools.mcutime(t, format).get())

  def is_config(self, name):
    syms = Gum.meta['symbols']
    return syms['__config_start']['adr'] <= syms[name]['adr'] < syms['__config_end']['adr'] and syms[name]['size']

  def is_mem(self, name):
    syms = Gum.meta['symbols']
    return syms[name]['mem'] == 'ram' and syms[name]['size']

  def is_reg(self, name):
    syms = Gum.meta['symbols']
    return syms[name]['mem'] == 'reg' and syms[name]['size']

  def get_config(self):
    conf = [ (x, self.read_symbol(x)) for x in Gum.meta['symbols'] if self.is_config(x) ]
    for i in conf: print(i[0], hex(i[1]))
    return conf

  def set_config(self, conf):
    for i in conf:
      if i[0] in Gum.meta['symbols']:
        print(i[0], hex(i[1]))
        self.write_symbol(i[0], i[1])
      else:
        print("Skipping:", i[0], hex(i[1]))

