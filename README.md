# Sistema de Localizaciu00f3n UWB para Fu00fatbol Sala

## Descripciu00f3n del Proyecto

Este proyecto de TFG (Trabajo Fin de Grado) desarrolla un sistema de localizaciu00f3n en tiempo real utilizando tecnologu00eda UWB (Ultra-Wideband) con ESP32, enfocado en aplicaciones para fu00fatbol sala y otros deportes en espacios interiores.

La tecnologu00eda UWB permite obtener mediciones de distancia con una precisiu00f3n de centu00edmetros, lo que la hace ideal para el seguimiento de jugadores en espacios donde el GPS tradicional no funciona correctamente.

## Estructura del Repositorio

- **Experimentos**: Contiene los diferentes experimentos realizados durante el desarrollo del proyecto.
  - **Experimento 1**: Sistema inicial donde el tag debe estar conectado al ordenador.
  - **ESP32 MQTT Experimento 2**: Sistema mejorado donde los anchors envu00edan datos vu00eda MQTT, permitiendo que el tag sea completamente mu00f3vil.

- **Documentaciu00f3n**: Informes, diagramas y documentaciu00f3n tu00e9cnica del proyecto.

- **Libreru00edas**: Libreru00edas necesarias para el funcionamiento del sistema.

- **Utilidades**: Scripts y herramientas auxiliares.

## Espacio de Pruebas

Para los experimentos se utiliza un espacio controlado de 3.45m de ancho y 5.1m de largo, con los anchors colocados en las siguientes coordenadas:

- Anclaje 10 (A): (0.0, 1.10)
- Anclaje 20 (B): (0.0, 4.55) 
- Anclaje 30 (C): (3.45, 3.5)
- Anclaje 40 (D): (3.45, 0.66)

## Tecnologu00edas Utilizadas

- **Hardware**:
  - ESP32 (para tags y anchors)
  - Mu00f3dulos UWB DW3000

- **Software**:
  - Arduino IDE
  - Libreru00eda DW3000 para ESP32
  - MQTT para comunicaciu00f3n inalu00e1mbrica
  - Python para procesamiento y visualizaciu00f3n de datos

## Protocolo de Ranging

El sistema utiliza el protocolo DS-TWR (Double-Sided Two-Way Ranging) para calcular distancias precisas entre el tag y los anchors. Este protocolo compensa los errores de deriva del reloj al realizar mediciones en ambas direcciones.

## Configuraciu00f3n de la Libreru00eda DW3000

La libreru00eda DW3000 requiere configuraciu00f3n explu00edcita mediante llamadas a mu00e9todos:

```cpp
DW3000.setChannel(5);           // Canal 5 (frecuencia ~6.5GHz)
DW3000.setPreambleLength(64);   // Longitud de preu00e1mbulo 
DW3000.setPreambleCode(9);      // Cu00f3digo de preu00e1mbulo 9
DW3000.setDatarate(0);          // Tasa de datos 110kbps
DW3000.writeSysConfig();        // Aplicar la configuraciu00f3n
```

## Evoluciu00f3n del Proyecto

El proyecto ha evolucionado a travu00e9s de diferentes experimentos:

1. **Experimento 1**: Sistema inicial donde el tag debe estar conectado al ordenador para procesar y visualizar los datos.

2. **Experimento 2**: Sistema mejorado donde los anchors envu00edan datos vu00eda MQTT, permitiendo que el tag sea completamente mu00f3vil y autu00f3nomo.

Los pru00f3ximos experimentos buscaru00e1n mejorar la precisiu00f3n, reducir la latencia y adaptar el sistema para su uso en entornos reales de fu00fatbol sala.

## Aplicaciones

Este sistema puede utilizarse para:

- Seguimiento de jugadores en tiempo real
- Anu00e1lisis de rendimiento deportivo
- Estadu00edsticas de movimiento y posicionamiento
- Visualizaciu00f3n de trayectorias y mapas de calor
