from onl.platform.base import *
from onl.platform.accton import *

class OnlPlatform_x86_64_accton_as9736_64d_r0(OnlPlatformAccton,
                                              OnlPlatformPortConfig_64x400_2x10):

    PLATFORM='x86-64-accton-as9736-64d-r0'
    MODEL="AS9736-64D"
    SYS_OBJECT_ID=".9736.64"

    def baseconfig(self):
        self.insmod('optoe')
        self.insmod('accton_i2c_psu')

        for m in [ 'sys', 'cpld', 'fan', 'psu', 'leds', 'fpga' ]:
            self.insmod("x86-64-accton-as9736-64d-%s.ko" % m)

        ########### initialize I2C bus 0 ###########
        # initialize multiplexer (PCA9548)
        self.new_i2c_device('pca9548', 0x72, 0)
        # initiate multiplexer (PCA9548)
        self.new_i2c_devices(
            [
                # initiate multiplexer (PCA9548)
                ('pca9548', 0x71, 3),
                ('pca9548', 0x71, 5),
                ('pca9548', 0x76, 9),
                ('pca9548', 0x70, 10),
                ('pca9548', 0x70, 11),
                ('pca9548', 0x70, 12),
                ('pca9548', 0x70, 18),
                ('pca9548', 0x70, 19),
                ]
            )

        self.new_i2c_devices([
            # initialize CPLD & FPGA
            ('as9736_64d_sys_cpld', 0x60, 6),
            ('sys_fpga', 0x60, 17),
            ('as9736_64d_fan', 0x33, 25),
            ('as9736_64d_pdb_cpld', 0x60, 36),
            ('as9736_64d_pdb_cpld', 0x60, 44),
            ('as9736_64d_scm_cpld', 0x35, 51),
            #('as9736_64d_udb_cpld1', 0x60, 60), read by pcie
            #('as9736_64d_udb_cpld2', 0x61, 61), read by pcie
            #('as9736_64d_ldb_cpld1', 0x61, 68), read by pcie
            #('as9736_64d_ldb_cpld2', 0x62, 69), read by pcie
            ])

        self.new_i2c_devices([
            # inititate TMP
            ('lm75', 0x48, 2),
            ('lm75', 0x49, 2),
            ('lm75', 0x4C, 14),
            ('lm75', 0x49, 27),
            ('lm75', 0x48, 27),
            ('lm75', 0x48, 34),
            ('lm75', 0x49, 42),
            ('lm75', 0x48, 57),
            ('lm75', 0x4C, 58),
            ('lm75', 0x4C, 65),
            ('lm75', 0x4D, 66),
         ])

        self.new_i2c_devices([
            # initiate PSU-1
            ('as9736_64d_psu1', 0x51, 41),
            ('acbel_fsh082', 0x59, 41),

            # initiate PSU-2

            ('as9736_64d_psu2', 0x50, 33),
            ('acbel_fsh082', 0x58, 33),
         ])

        self.new_i2c_device('as973d_64d_sys', 0x51, 20)

        # initialize pca9548 idle_state
        subprocess.call('echo -2 | tee /sys/bus/i2c/drivers/pca954x/*-00*/idle_state > /dev/null', shell=True)

        return True
