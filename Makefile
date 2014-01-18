CURRENT = $(shell cat /home/andrej/ti-dvsdk_omapl138-evm_4_02_00_06/linux-davinci/linux-davinci/include/config/kernel.release)
KDIR = /home/andrej/ti-dvsdk_omapl138-evm_4_02_00_06/linux-davinci/linux-davinci
PWD = $(shell pwd)
DEST = $(shell pwd)
TARGET = pga2500
obj-m      := $(TARGET).o
default:
	   $(MAKE) -C $(KDIR) ARCH=arm CROSS_COMPILE=/home/andrej/tools/codesourcery/bin/arm-none-linux-gnueabi- M=$(PWD) modules

clean:
	@rm -f *.o .*.cmd .*.flags *.mod.c *.order
	@rm -f .*.*.cmd *.symvers *~ *.*~ TODO.*
	@rm -fR .tmp*
	@rm -rf .tmp_versions
