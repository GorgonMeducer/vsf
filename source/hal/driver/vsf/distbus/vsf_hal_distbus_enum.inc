#if VSF_HAL_USE_IO == ENABLED && VSF_HAL_DISTBUS_USE_IO == ENABLED
    __VSF_HAL_DISTBUS_ENUM(io)
#endif
#if VSF_HAL_USE_GPIO == ENABLED && VSF_HAL_DISTBUS_USE_GPIO == ENABLED
    __VSF_HAL_DISTBUS_ENUM(gpio)
#endif
#if VSF_HAL_USE_I2C == ENABLED && VSF_HAL_DISTBUS_USE_I2C == ENABLED
    __VSF_HAL_DISTBUS_ENUM(i2c)
#endif
#if VSF_HAL_USE_SPI == ENABLED && VSF_HAL_DISTBUS_USE_SPI == ENABLED
    __VSF_HAL_DISTBUS_ENUM(spi)
#endif
#if VSF_HAL_USE_USART == ENABLED && VSF_HAL_DISTBUS_USE_USART == ENABLED
    __VSF_HAL_DISTBUS_ENUM(usart)
#endif
#if VSF_HAL_USE_MMC == ENABLED && VSF_HAL_DISTBUS_USE_MMC == ENABLED
    __VSF_HAL_DISTBUS_ENUM(mmc)
#endif
#if VSF_HAL_USE_ADC == ENABLED && VSF_HAL_DISTBUS_USE_ADC == ENABLED
    __VSF_HAL_DISTBUS_ENUM(adc)
#endif
#if VSF_HAL_USE_DAC == ENABLED && VSF_HAL_DISTBUS_USE_DAC == ENABLED
    __VSF_HAL_DISTBUS_ENUM(dac)
#endif
#if VSF_HAL_USE_PWM == ENABLED && VSF_HAL_DISTBUS_USE_PWM == ENABLED
    __VSF_HAL_DISTBUS_ENUM(pwm)
#endif
#if VSF_HAL_USE_I2S == ENABLED && VSF_HAL_DISTBUS_USE_I2S == ENABLED
    __VSF_HAL_DISTBUS_ENUM(i2s)
#endif
#if VSF_USE_USB_DEVICE == ENABLED && VSF_HAL_USE_USBD == ENABLED && VSF_HAL_DISTBUS_USE_USBD == ENABLED
    __VSF_HAL_DISTBUS_ENUM(usbd)
#endif
#if VSF_USE_USB_HOST == ENABLED && VSF_HAL_USE_USBH == ENABLED && VSF_HAL_DISTBUS_USE_USBH == ENABLED
    __VSF_HAL_DISTBUS_ENUM(usbh)
#endif

#undef __VSF_HAL_DISTBUS_ENUM