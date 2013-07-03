/*
 * pga2500_driver.h
 *
 *  Created on: 23.04.2012
 *      Author: korrav
 */

#ifndef PGA2500_DRIVER_H_
#define PGA2500_DRIVER_H_
#include <linux/ioctl.h>

//КОМАНДЫ IOCTL
#define ID_IO_PGA2500 201	//идентификатор для команд ioctl
//ПОЛЬЗАВАТЕЛЬСКИЕ НАСТРОЙКИ
#define AMPLIFIER_NAME "pga2500"	//имя узла усилителя устройства
#define DEV_CLASS_AMP "MAD - amplifier"	//имя класса, к которому принадлежат устройства акустического модуля
#define NUM_AMPLIFIER 4	//количество микросхем усилителей на плате
#define MAX_GAIN 63	//максимальная величина коэффициента напряжения
#define MIN_GAIN 0	//минимальная величина коэффициента напряжения


#endif /* PGA2500_DRIVER_H_ */
