from onl.platform.base import *
from onl.platform.accton import *

class OnlPlatform_x86_64_accton_as7816_64x_r0(OnlPlatformAccton,
                                              OnlPlatformPortConfig_64x100):
    PLATFORM='x86-64-accton-as7816-64x-r0'
    MODEL="AS7816-64x"
    SYS_OBJECT_ID=".7816.64"
    
    def baseconfig(self):
        self.insmod('optoe')
        self.insmod('ym2651y')
        self.insmod('accton_i2c_cpld')
        for m in [ 'fan', 'cpld1', 'leds' ]:
            self.insmod("x86-64-accton-as7816-64x-%s.ko" % m)

        ########### initialize I2C bus 0 ###########
        self.new_i2c_devices([
                # initialize multiplexer (PCA9548)
                ('pca9548', 0x77, 0),

                # initiate leaf multiplexer (PCA9548)
                ('pca9548', 0x71, 1),
                ('pca9548', 0x76, 1),
                ('pca9548', 0x73, 1),

                # initiate PSU-1
                ('24c02', 0x53, 10),
                ('ym2851', 0x5b, 10),

                # initiate PSU-2
                ('24c02', 0x50, 9),
                ('ym2851', 0x58, 9),

                # initiate chassis fan
                ('as7816_64x_fan', 0x68, 17),

                # inititate LM75
                ('lm75', 0x48, 18),
                ('lm75', 0x49, 18),
                ('lm75', 0x4a, 18),
                ('lm75', 0x4b, 18),
                ('lm75', 0x4d, 17),
                ('lm75', 0x4e, 17),

                #initiate CPLD
                ('as7816_64x_cpld1', 0x60, 19),
                ('accton_i2c_cpld', 0x62, 20),
                ('accton_i2c_cpld', 0x64, 21),
                ('accton_i2c_cpld', 0x66, 22),

                # initiate leaf multiplexer (PCA9548)
                ('pca9548', 0x70, 2),
                ('pca9548', 0x71, 2),
                ('pca9548', 0x72, 2),
                ('pca9548', 0x73, 2),
                ('pca9548', 0x74, 2),
                ('pca9548', 0x75, 2),
                ('pca9548', 0x76, 2),

                #('24c02', 0x56, 0),
                ])

        subprocess.call('echo -2 | tee /sys/bus/i2c/drivers/pca954x/*-00*/idle_state > /dev/null', shell=True)

        # Front-panel port -> mux-leaf i2c bus. Indexed by (port - 1).
        # The pca9548 leaf channels are not wired in port order, so the
        # mapping is irregular (note the 5<->6, 7<->8 pair swaps, and the
        # 4-port blocks that are out of sequence across the tree).
        port_i2c_bus = [
            37, 38, 39, 40, 42, 41, 44, 43,  # port  1-8
            33, 34, 35, 36, 45, 46, 47, 48,  # port  9-16
            49, 50, 51, 52, 61, 62, 63, 64,  # port 17-24
            53, 54, 55, 56, 57, 58, 59, 60,  # port 25-32
            69, 70, 71, 72, 77, 78, 79, 80,  # port 33-40
            65, 66, 67, 68, 73, 74, 75, 76,  # port 41-48
            85, 86, 87, 88, 31, 32, 29, 30,  # port 49-56
            81, 82, 83, 84, 25, 26, 27, 28,  # port 57-64
        ]
        for port in range(1, 65):
            bus = port_i2c_bus[port - 1]
            self.new_i2c_device('optoe1', 0x50, bus)
            subprocess.call('echo port%d > /sys/bus/i2c/devices/%d-0050/port_name' % (port, bus), shell=True)

        return True
