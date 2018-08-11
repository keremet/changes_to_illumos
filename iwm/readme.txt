Для определения идентификаторов устройства на шине PCI
scanpci
затем для уточнения
prtconf -v|less

Установка драйвера
add_drv -i '"pci8086,95a"' iwm
Он пропишется в /etc/driver_aliases


скрипты:
iwm_pci_config_setup.d - для выяснения причины ошибки pci_config_setup при установке драйвера
вместо null_bus_map должна вызываться функция pcieb_bus_map,если правильно прописаны идентификаторы устройства на шине PCI


Сканирование сетей
# dladm scan-wifi iwm0

Трасса сканирования
# ./iwm.d 
dtrace: script './iwm.d' matched 46 probes
CPU FUNCTION                                 
  1  -> iwm_m_propinfo                        
  1  <- iwm_m_propinfo                        
  1  -> iwm_m_getprop                         
  1  <- iwm_m_getprop                         
  1  -> iwm_m_start                           
  1  <- iwm_m_start                           
  2  -> iwm_m_ioctl                           
  2  <- iwm_m_ioctl                           
  1  -> iwm_m_stop                            
  1  <- iwm_m_stop                            

