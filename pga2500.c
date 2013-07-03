/*
 * pga2500_driver.cpp
 *
 *  Created on: 23.04.2012
 *      Author: korrav
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <mach/mux.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <asm/string.h>
#include <asm/gpio.h>	//перед компиляцией изменить на <asm/gpio.h>
#include <asm/uaccess.h>
#include "pga2500.h"
MODULE_LICENSE( "GPL" );
MODULE_AUTHOR( "Andrej Korobchenko <korrav@yandex.ru>" );
MODULE_VERSION( "0:1.0" );


static char device_name[]=AMPLIFIER_NAME; //имя устройства
static dev_t dev_pga2500; //содержит старший и младший номер устройства
static struct cdev cdev_pga2500;
static struct class* devclass;	//класс устройств, к которому принадлежит pga2500
static DEFINE_MUTEX(device_lockk);	//мьютекс, применяемый для блокировки критически важного сегмента кода

struct pga2500_data {	//специфичная для драйвера pga2500 структура
	dev_t devt;
	struct spi_device* spi;
	spinlock_t	spi_lock;	//спин-блокировка
	unsigned users;			//количество процессов, использующих сейчас данное устройство
	short buffer[NUM_AMPLIFIER];		//буфер, учавствующий в передаче данных драйверу контроллера spi, а также хранящий текущее состояние регистров усилителей
} *pga2500_status;

//ФУНКЦИИ СТРУКТУРЫ FILE_OPERATIONS

static int pga2500_open(struct inode *inode, struct file *filp)
{
	spin_lock(&pga2500_status->spi_lock);
	filp->private_data = pga2500_status;
	pga2500_status->users++;
	spin_unlock(&pga2500_status->spi_lock);
	nonseekable_open(inode, filp);	//сообщение ядру, что данное устройство не поддерживает произвольный доступ к данным
	return (0);

}
static int pga2500_release (struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	mutex_lock(&device_lockk);
	pga2500_status->users--;
	mutex_unlock(&device_lockk);
	return (0);
}

static void pga2500_complete(void *arg)	//функция обратного вызова по окончанию обработки сообщения контроллером spi
{
	complete(arg);
}

static ssize_t pga2500_write(struct file *filp, const char __user *buf, size_t count, loff_t *fpos)
{
	short buf_term[4];	//буфер, куда копируются сообщения из пользовательского пространства, и где они проходят предварительное форматирование
	int status = 0;
	int i=0;
	struct spi_transfer t = {		//формируется передача
			.tx_buf = pga2500_status->buffer,
			.len = (NUM_AMPLIFIER * sizeof(short)),
	};
	struct spi_message	m;	// сообщение
	DECLARE_COMPLETION_ONSTACK(done);	//объявляется и инициализуется условная переменная
	//проверка на достоверность переданного буфера
	if (count > sizeof (pga2500_status->buffer))
		return (-EMSGSIZE);
	else if (count%sizeof(short))
		return (-EPROTO);
	printk(KERN_INFO "Conditions sizeof are executed\n");
	if (copy_from_user(buf_term, buf, count))
		return (-EFAULT);
	printk(KERN_INFO "Conditions copy_from_user are executed\n");
	for (i=0; i < (count/sizeof(short)); i++)
	{
		printk(KERN_INFO "buf_term[%d] = %d\n", i, buf_term[i]);
		if (buf_term[i] < MIN_GAIN || buf_term[i] > MAX_GAIN)
			return (-EINVAL);
	}
	printk(KERN_INFO "Conditions validate value are executed\n");

	//передача сообщения драйверу контроллера
	mutex_lock(&device_lockk);
	for (i=0; i<count; i++) {
		pga2500_status->buffer[i] |= buf_term[i];
		pga2500_status->buffer[i] |= 0x0a00;
	}

	spi_message_init(&m);	//инициализация сообщения
	spi_message_add_tail(&t, &m);	//постановка передачи в очередь сообщения
	m.complete = pga2500_complete;
	m.context = &done;
	if (pga2500_status->spi == NULL)
		status = -ESHUTDOWN;
	else
		status = spi_async(pga2500_status->spi, &m);	//передача сообщения
	if (status == 0) {
		wait_for_completion(&done);	//ожидание обработки сообщения контроллером spi
		status = m.status;
		if (status == 0)
			status = m.actual_length;
	}

	if (gpio_direction_output(52, 0) < 0) {	//деактивирование	0dB_U5
		printk("ERROR in case of assignment of level of an GPIO %d\n", 52);
		return (status);
	}

	if (gpio_direction_output(53, 0) < 0) {	//деактивирование	0dB_U2
		printk("ERROR in case of assignment of level of an GPIO %d\n", 53);
		return (status);
	}

	if (gpio_direction_output(54, 0) < 0) {	//деактивирование	0dB_U4
		printk("ERROR in case of assignment of level of an GPIO %d\n", 54);
		return (status);
	}

	if (gpio_direction_output(55, 0) < 0) {	//деактивирование	0dB_U3
		printk("ERROR in case of assignment of level of an GPIO %d\n", 55);
		return (status);
	}

	mutex_unlock(&device_lockk);
	return (status);
}

//ФУНКЦИИ СТРУКТУРЫ SPI_DRIVER
static int	__devinit pga2500_probe(struct spi_device *spi)
{
	int status, dev;
	//регистрация устройства
	dev = device_create(devclass, NULL, dev_pga2500, NULL, AMPLIFIER_NAME);	//создание устройства
	status = IS_ERR(dev) ? PTR_ERR(dev) : 0;
	if(status != 0)
	{
		printk(KERN_ERR "The device_create function failed\n");
		return (status);
	}
	//инициализация членов структуры состояния драйвера
	mutex_lock(&device_lockk);
	pga2500_status->users = 0;
	pga2500_status->spi = spi;
	spi->bits_per_word = 16;
	spi->max_speed_hz = 700000;
	spin_lock_init(&pga2500_status->spi_lock);
	memset(pga2500_status->buffer, 0, sizeof(pga2500_status->buffer));
	spi_set_drvdata(spi, pga2500_status);	//присваевает указателю spi->dev->driver_data значение pga2500_status
	mutex_unlock(&device_lockk);
	return (0);
}

static int __devexit pga2500_remove(struct spi_device *spi)
{
	mutex_lock(&device_lockk);
	pga2500_status->spi = NULL;
	spi_set_drvdata(spi, NULL);
	device_destroy(devclass, dev_pga2500);
	mutex_unlock(&device_lockk);
	return (0);
}

//СТРУКТУРА FILE_OPERATIONS
static  const struct file_operations pga_fops= {
		.owner = THIS_MODULE,
		.open = pga2500_open,			//open
		.release = pga2500_release,		//release
		.write = pga2500_write,			//write
};

//СТРУКТУРА SPI_DRIVER
struct spi_driver spi_pga2500_driver = {
		.driver =
		{
				.name = AMPLIFIER_NAME,
				.owner = THIS_MODULE,
		},
		.probe = pga2500_probe,
		.remove = pga2500_remove,

};

//ФУНКЦИИ ИНИЦИАЛИЗАЦИИ И ВЫКЛЮЧЕНИЯ МОДУЛЯ ДРАЙВЕРА

static int __init pga2500_init(void)
{
	int status;
	int gpio_num;	//хранит номер вывода
	printk(KERN_INFO "size short in Kernal = %d\n", sizeof(short));
	short da850_pga2500_pins[]= {
			DA850_PGA2500_OVR_U3,
			DA850_PGA2500_OVR_U4,
			DA850_PGA2500_OVR_U2,
			DA850_PGA2500_OVR_U5,
			DA850_PGA2500_DCEN_U2,
			DA850_PGA2500_DCEN_U5,
			DA850_PGA2500_DCEN_U4,
			DA850_PGA2500_DCEN_U3,
			DA850_PGA2500_0dB_U5,
			DA850_PGA2500_0dB_U2,
			DA850_PGA2500_0dB_U4,
			DA850_PGA2500_0dB_U3,
			DA850_PGA2500_ZCEN_U5,
			DA850_PGA2500_ZCEN_U2,
			DA850_PGA2500_ZCEN_U4,
			DA850_PGA2500_ZCEN_U3,
			-1,
	};

	//конфигурирование gpio выводов
	status = davinci_cfg_reg_list(da850_pga2500_pins);	//конфигурирование мультиплексора функциональности выводов
	if (status<0)
	{
		printk("pin could not be muxed for GPIO functionality");
		return (status);
	}

	gpio_num = 80;								//конфигурирование	OVR_U3
	status = gpio_request(gpio_num, "OVR_U3");
	if (status < 0) {
		printk("ERROR can not open GPIO %d\n", gpio_num);
		return (status);
	}
	status = gpio_direction_input(gpio_num);
	if (status < 0) {
		printk("ERROR in case of assignment of level of an GPIO %d\n", gpio_num);
		return (status);
	}

	gpio_num = 81;								//конфигурирование	OVR_U4
	status = gpio_request(gpio_num, "OVR_U4");
	if (status < 0) {
		printk("ERROR can not open GPIO %d\n", gpio_num);
		return (status);
	}
	status = gpio_direction_input(gpio_num);
	if (status < 0) {
		printk("ERROR in case of assignment of level of an GPIO %d\n", gpio_num);
		return (status);
	}

	gpio_num = 82;								//конфигурирование	OVR_U2
	status = gpio_request(gpio_num, "OVR_U2");
	if (status < 0) {
		printk("ERROR can not open GPIO %d\n", gpio_num);
		return (status);
	}
	status = gpio_direction_input(gpio_num);
	if (status < 0) {
		printk("ERROR in case of assignment of level of an GPIO %d\n", gpio_num);
		return (status);
	}

	gpio_num = 83;								//конфигурирование	OVR_U5
	status = gpio_request(gpio_num, "OVR_U5");
	if (status < 0) {
		printk("ERROR can not open GPIO %d\n", gpio_num);
		return (status);
	}
	status = gpio_direction_input(gpio_num);
	if (status < 0) {
		printk("ERROR in case of assignment of level of an GPIO %d\n", gpio_num);
		return (status);
	}

	gpio_num = 59;								//конфигурирование	DCEN_U2
	status = gpio_request(gpio_num, "DCEN_U2");
	if (status < 0) {
		printk("ERROR can not open GPIO %d\n", gpio_num);
		return (status);
	}
	status = gpio_direction_output(gpio_num, 0);
	if (status < 0) {
		printk("ERROR in case of assignment of level of an GPIO %d\n", gpio_num);
		return (status);
	}

	gpio_num = 58;								//конфигурирование	DCEN_U5
	status = gpio_request(gpio_num, "DCEN_U5");
	if (status < 0) {
		printk("ERROR can not open GPIO %d\n", gpio_num);
		return (status);
	}
	status = gpio_direction_output(gpio_num, 0);
	if (status < 0) {
		printk("ERROR in case of assignment of level of an GPIO %d\n", gpio_num);
		return (status);
	}

	gpio_num = 60;								//конфигурирование	DCEN_U4
	status = gpio_request(gpio_num, "DCEN_U4");
	if (status < 0) {
		printk("ERROR can not open GPIO %d\n", gpio_num);
		return (status);
	}
	status = gpio_direction_output(gpio_num, 0);
	if (status < 0) {
		printk("ERROR in case of assignment of level of an GPIO %d\n", gpio_num);
		return (status);
	}

	gpio_num = 61;								//конфигурирование	DCEN_U3
	status = gpio_request(gpio_num, "DCEN_U3");
	if (status < 0) {
		printk("ERROR can not open GPIO %d\n", gpio_num);
		return (status);
	}
	status = gpio_direction_output(gpio_num, 0);
	if (status < 0) {
		printk("ERROR in case of assignment of level of an GPIO %d\n", gpio_num);
		return (status);
	}

	gpio_num = 52;								//конфигурирование	0dB_U5
	status = gpio_request(gpio_num, "0dB_U5");
	if (status < 0) {
		printk("ERROR can not open GPIO %d\n", gpio_num);
		return (status);
	}
	status = gpio_direction_output(gpio_num, 1);
	if (status < 0) {
		printk("ERROR in case of assignment of level of an GPIO %d\n", gpio_num);
		return (status);
	}

	gpio_num = 53;								//конфигурирование	0dB_U2
	status = gpio_request(gpio_num, "0dB_U2");
	if (status < 0) {
		printk("ERROR can not open GPIO %d\n", gpio_num);
		return (status);
	}
	status = gpio_direction_output(gpio_num, 1);
	if (status < 0) {
		printk("ERROR in case of assignment of level of an GPIO %d\n", gpio_num);
		return (status);
	}

	gpio_num = 54;								//конфигурирование	0dB_U4
	status = gpio_request(gpio_num, "0dB_U4");
	if (status < 0) {
		printk("ERROR can not open GPIO %d\n", gpio_num);
		return (status);
	}
	status = gpio_direction_output(gpio_num, 1);
	if (status < 0) {
		printk("ERROR in case of assignment of level of an GPIO %d\n", gpio_num);
		return (status);
	}

	gpio_num = 55;								//конфигурирование	0dB_U3
	status = gpio_request(gpio_num, "0dB_U3");
	if (status < 0) {
		printk("ERROR can not open GPIO %d\n", gpio_num);
		return (status);
	}
	status = gpio_direction_output(gpio_num, 1);
	if (status < 0) {
		printk("ERROR in case of assignment of level of an GPIO %d\n", gpio_num);
		return (status);
	}

	gpio_num = 48;								//конфигурирование	ZCEN_U5
	status = gpio_request(gpio_num, "ZCEN_U5");
	if (status < 0) {
		printk("ERROR can not open GPIO %d\n", gpio_num);
		return (status);
	}
	status = gpio_direction_output(gpio_num, 0);
	if (status < 0) {
		printk("ERROR in case of assignment of level of an GPIO %d\n", gpio_num);
		return (status);
	}

	gpio_num = 49;								//конфигурирование	ZCEN_U2
	status = gpio_request(gpio_num, "ZCEN_U2");
	if (status < 0) {
		printk("ERROR can not open GPIO %d\n", gpio_num);
		return (status);
	}
	status = gpio_direction_output(gpio_num, 0);
	if (status < 0) {
		printk("ERROR in case of assignment of level of an GPIO %d\n", gpio_num);
		return (status);
	}

	gpio_num = 50;								//конфигурирование	ZCEN_U4
	status = gpio_request(gpio_num, "ZCEN_U4");
	if (status < 0) {
		printk("ERROR can not open GPIO %d\n", gpio_num);
		return (status);
	}
	status = gpio_direction_output(gpio_num, 0);
	if (status < 0) {
		printk("ERROR in case of assignment of level of an GPIO %d\n", gpio_num);
		return (status);
	}

	gpio_num = 51;								//конфигурирование	ZCEN_U3
	status = gpio_request(gpio_num, "ZCEN_U3");
	if (status < 0) {
		printk("ERROR can not open GPIO %d\n", gpio_num);
		return (status);
	}
	status = gpio_direction_output(gpio_num, 0);
	if (status < 0) {
		printk("ERROR in case of assignment of level of an GPIO %d\n", gpio_num);
		return (status);
	}

	//получение идентификатора для устройства
	if(alloc_chrdev_region(&dev_pga2500, 0, 1, device_name))
	{
		printk(KERN_ERR "The request_mem_region function failed\n");
		return (1);
	}
	//регистрация символьного устройства
	cdev_init(&cdev_pga2500, &pga_fops);
	cdev_pga2500.owner = THIS_MODULE;
	if (cdev_add(&cdev_pga2500, dev_pga2500, 1))
	{
		unregister_chrdev_region(dev_pga2500, 1);
		printk(KERN_ERR "The cdev_add function failed\n");
		return(1);
	}

	//регистрация класса устройств
	devclass = class_create( THIS_MODULE, DEV_CLASS_AMP);	//создание класса
	if (IS_ERR(devclass))
	{
		printk(KERN_ERR "The class_create function failed\n");
		unregister_chrdev_region(dev_pga2500, 1);
		return (PTR_ERR(devclass));
	}

	//выделение памяти для структуры состояния драйвера
	pga2500_status = kzalloc(sizeof(struct pga2500_data), GFP_KERNEL);
	if (!pga2500_status)
	{
		return (-ENOMEM);
	}

	//регистрация spi драйвера
	if (spi_register_driver(&spi_pga2500_driver))
	{
		printk(KERN_ERR "The spi_register_driver function failed\n");
		unregister_chrdev_region(dev_pga2500, 1);
		class_destroy(devclass);
		return (1);
	}
	return (0);
}
module_init( pga2500_init);

static void __exit pga2500_exit(void)
{
	kfree(pga2500_status);
	spi_unregister_driver(&spi_pga2500_driver);
	class_destroy(devclass);
	unregister_chrdev_region(dev_pga2500, 1);
}
module_exit(pga2500_exit);
