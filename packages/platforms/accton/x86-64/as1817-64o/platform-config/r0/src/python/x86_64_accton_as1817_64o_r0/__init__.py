from onl.platform.base import *
from onl.platform.accton import *
from time import sleep

init_ipmi_dev = [
    'echo "remove,kcs,i/o,0xca2" > /sys/module/ipmi_si/parameters/hotmod',
    'echo "add,kcs,i/o,0xca2" > /sys/module/ipmi_si/parameters/hotmod']

ATTEMPTS = 5
INTERVAL = 3

def init_ipmi_dev_intf():
    attempts = ATTEMPTS
    interval = INTERVAL

    while attempts:
        if os.path.exists('/dev/ipmi0') or os.path.exists('/dev/ipmidev/0'):
            return (True, (ATTEMPTS - attempts) * interval)

        for cmd in init_ipmi_dev:
            subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE).communicate()

        attempts -= 1
        sleep(interval)

    return (False, ATTEMPTS * interval)

def init_ipmi_oem_cmd():
    attempts = ATTEMPTS
    interval = INTERVAL

    while attempts:
        process = subprocess.Popen("ipmitool raw 0x34 0x95", shell=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        process.communicate()
        if process.returncode == 0:
            return (True, (ATTEMPTS - attempts) * interval)

        attempts -= 1
        sleep(interval)

    return (False, ATTEMPTS * interval)

def init_ipmi():
    attempts = ATTEMPTS
    interval = 60

    while attempts:
        attempts -= 1

        (status, elapsed_dev) = init_ipmi_dev_intf()
        if not status:
            sleep(interval - elapsed_dev)
            continue

        (status, elapsed_oem) = init_ipmi_oem_cmd()
        if not status:
            sleep(interval - elapsed_dev - elapsed_oem)
            continue

        print('IPMI dev interface is ready.')
        return True

    print('Failed to initialize IPMI dev interface')
    return False

class OnlPlatform_x86_64_accton_as1817_64o_r0(OnlPlatformAccton,
                                              OnlPlatformPortConfig_64x800_2x25):

    PLATFORM='x86-64-accton-as1817-64o-r0'
    MODEL="AS1817-64O"
    SYS_OBJECT_ID=".1817.64"

    def modprobe(self, module):
        subprocess.check_call("modprobe %s" % module, shell=True)

    def baseconfig(self):
        if init_ipmi() is not True:
            return False

        self.modprobe('optoe')

        # Load platform drivers
        for m in ['fpga', 'i2c-ocores', 'fan', 'psu', 'thermal', 'sys', 'leds']:
            self.insmod("x86-64-accton-as1817-64o-%s" % m)

        # Wait for FPGA driver to create i2c adapters
        sleep(2)

        # Enable clock source
        subprocess.call('ipmitool raw 0x34 0x23 0x65 0x12 0x3f', shell=True)

        # Enable EFUSE for all OSFP ports
        for reg in range(0x70, 0x78):
            subprocess.call('ipmitool raw 0x34 0x23 0x61 0x%02x 0xff' % reg,
                            shell=True)

        # Release reset for all OSFP ports
        for port in range(1, 65):
            subprocess.call('echo 0 > /sys/devices/platform/as1817_64o_fpga/module_reset_%d' % port,
                            shell=True)

        # i2c bus layout: bus0=I801, bus1=iSMT, bus2~67=ocores (port1~66)
        bus_start = 2

        # Instantiate optoe devices on each port
        for port in range(1, 67):
            bus = bus_start + port - 1
            devtype = 'optoe3' if port <= 64 else 'optoe2'
            self.new_i2c_device(devtype, 0x50, bus)
            subprocess.call('echo port%d > /sys/bus/i2c/devices/%d-0050/port_name' % (port, bus),
                            shell=True)

        return True
