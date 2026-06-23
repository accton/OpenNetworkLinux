from onl.platform.base import *
from onl.platform.accton import *

def get_i2c_bus_num_offset():
    cmd = 'cat /sys/bus/i2c/devices/i2c-0/name'
    process = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = process.communicate()
    return -1 if b'iSMT' in stdout else 0 # the iSMT adapter may get i2c bus 0, hence the offset -1

class OnlPlatform_x86_64_accton_as5812_54x_r0(OnlPlatformAccton,
                                              OnlPlatformPortConfig_48x10_6x40):

    PLATFORM='x86-64-accton-as5812-54x-r0'
    MODEL="AS5812-54X"
    SYS_OBJECT_ID=".5812.54.1"

    def baseconfig(self):
        self.insmod('optoe')
        self.insmod('cpr_4011_4mxx')
        self.insmod("ym2651y")
        for m in [ 'cpld', 'fan', 'psu', 'leds' ]:
            self.insmod("x86-64-accton-as5812-54x-%s.ko" % m)

        # 4.14 enumerates i801 as i2c-0 (CPLDs live there); 6.12 enumerates
        # iSMT as i2c-0 first, pushing i801 to i2c-1. CPLDs are wired to the
        # i801 controller, so re-anchor them to whichever bus i801 ended up
        # on. `bus_offset` is 0 on 4.14 and -1 on 6.12.
        bus_offset = get_i2c_bus_num_offset()
        i801_bus = 0 - bus_offset

        ########### initialize I2C bus 0 ###########

        # initialize CPLDs (on i801)
        self.new_i2c_devices(
            [
                ('as5812_54x_cpld1', 0x60, i801_bus),
                ('as5812_54x_cpld2', 0x61, i801_bus),
                ('as5812_54x_cpld3', 0x62, i801_bus),
                ]
            )

        # initialize SFP devices
        for port in range(1, 49):
            self.new_i2c_device('optoe2', 0x50, port+1)
            subprocess.call('echo port%d > /sys/bus/i2c/devices/%d-0050/port_name' % (port, port+1), shell=True)

        # Initialize QSFP devices
        for port in range(49, 55):
            self.new_i2c_device('optoe1', 0x50, port+1)
        subprocess.call('echo port49 > /sys/bus/i2c/devices/50-0050/port_name', shell=True)
        subprocess.call('echo port52 > /sys/bus/i2c/devices/51-0050/port_name', shell=True)
        subprocess.call('echo port50 > /sys/bus/i2c/devices/52-0050/port_name', shell=True)
        subprocess.call('echo port53 > /sys/bus/i2c/devices/53-0050/port_name', shell=True)
        subprocess.call('echo port51 > /sys/bus/i2c/devices/54-0050/port_name', shell=True)
        subprocess.call('echo port54 > /sys/bus/i2c/devices/55-0050/port_name', shell=True)

        ########### initialize I2C bus 1 (iSMT) ###########
        # The iSMT adapter probe order varies between kernels: 4.14 had iSMT
        # as i2c-1, 6.12 enumerates iSMT first as i2c-0. Without `bus_offset`
        # the multiplexer and IDPROM get instantiated on the i801 SMBus
        # where they NACK (pca954x probe failed, no eeprom sysfs node).
        self.new_i2c_devices(
            [
                # initiate multiplexer (PCA9548)
                ('pca9548', 0x70, 1 + bus_offset),

                # initiate PSU-1 AC Power
                ('as5812_54x_psu1', 0x38, 57),
                ('cpr_4011_4mxx',  0x3c, 57),
                ('as5812_54x_psu1', 0x50, 57),
                ('ym2401',  0x58, 57),

                # initiate PSU-2 AC Power
                ('as5812_54x_psu2', 0x3b, 58),
                ('cpr_4011_4mxx',  0x3f, 58),
                ('as5812_54x_psu2', 0x53, 58),
                ('ym2401',  0x5b, 58),

                # initiate lm75
                ('lm75', 0x48, 61),
                ('lm75', 0x49, 62),
                ('lm75', 0x4a, 63),

                # System EEPROM
                ('24c02', 0x57, 1 + bus_offset),
                ]
            )

        # Leave the pca9548 selected on the last-used channel rather than
        # deselecting it after every i2c transaction. Without this, every
        # downstream xfer on bus 57/58/61/62/63 toggles the mux on iSMT,
        # which on 6.12 stresses the iSMT timing enough to occasionally
        # drop transactions. Matches the AS5835 / SONiC convention.
        subprocess.call('echo -2 | tee /sys/bus/i2c/drivers/pca954x/*-00*/idle_state > /dev/null', shell=True)

        return True
