#!/usr/bin/env python3
import serial
import serial.tools.list_ports
import time
import threading
import os
import datetime
import argparse
import sys
import signal
import re

class CsvLogger:
    def __init__(self, port=None, baud_rate=115200, output_dir="data_recordings"):
        self.port = port
        self.baud_rate = baud_rate
        self.output_dir = output_dir
        self.ser = None
        self.is_connected = False
        self.is_logging = False
        self.logging_thread = None
        self.csv_file = None
        self.records_count = 0
        self.verbose_output = False  # Control para la salida detallada
        self.debug_mode = False      # Modo de depuración para ver más información
        self.last_positions = {"10": (0.0, 1.10), "20": (0.0, 4.55), "30": (3.45, 3.5), "40": (3.45, 0.66)}
        
        # Crear directorio de salida si no existe
        if not os.path.exists(output_dir):
            os.makedirs(output_dir)
            print(f"Directorio creado: {output_dir}")
    
    def list_available_ports(self):
        """Lista los puertos seriales disponibles."""
        ports = serial.tools.list_ports.comports()
        if not ports:
            print("No se encontraron puertos disponibles.")
            return []
        
        print("Puertos disponibles:")
        for i, port in enumerate(ports):
            print(f"{i+1}. {port.device} - {port.description}")
        
        return [port.device for port in ports]
    
    def connect(self, port=None):
        """Conecta al puerto serial especificado o solicita elegir uno."""
        if self.is_connected:
            print("Ya está conectado. Desconecte primero.")
            return True
        
        # Si no se especifica puerto, mostrar lista para elegir
        if port is None:
            if self.port is None:
                ports = self.list_available_ports()
                if not ports:
                    return False
                
                if len(ports) == 1:
                    self.port = ports[0]
                    print(f"Seleccionado único puerto disponible: {self.port}")
                else:
                    try:
                        selection = int(input("Seleccione un puerto (número): "))
                        if 1 <= selection <= len(ports):
                            self.port = ports[selection-1]
                        else:
                            print("Selección inválida.")
                            return False
                    except ValueError:
                        print("Entrada inválida.")
                        return False
            port = self.port
        else:
            self.port = port
        
        # Intentar conectar al puerto
        try:
            self.ser = serial.Serial(port, self.baud_rate, timeout=1)
            self.is_connected = True
            print(f"Conectado a {port} a {self.baud_rate} baudios.")
            # Limpiar buffer al inicio para evitar datos parciales
            time.sleep(0.2)
            if self.ser.in_waiting:
                self.ser.reset_input_buffer()
            return True
        except serial.SerialException as e:
            print(f"Error al conectar: {e}")
            self.is_connected = False
            return False
    
    def disconnect(self):
        """Desconecta del puerto serial."""
        if not self.is_connected:
            print("No hay conexión activa.")
            return
        
        self.stop_logging()  # Detener logging si está activo
        
        if self.ser:
            self.ser.close()
            self.ser = None
        
        self.is_connected = False
        print("Desconectado del puerto serial.")
    
    def start_logging(self):
        """Inicia la captura y grabación de datos CSV."""
        if not self.is_connected:
            print("No hay conexión al dispositivo.")
            return False
        
        if self.is_logging:
            print("Ya se está grabando datos CSV.")
            return True
        
        # Limpiar buffer
        time.sleep(0.1)
        if self.ser and self.ser.in_waiting:
            self.ser.reset_input_buffer()
            
        # Enviar comando para iniciar grabación en el ESP32
        if not self.send_command("/start_csv"):
            print("Error al enviar comando de inicio.")
            return False
            
        print("Comando de inicio enviado al ESP32.")
        time.sleep(0.5)  # Esperar a que el ESP32 procese el comando
            
        self.is_logging = True
        self.records_count = 0
        self.logging_thread = threading.Thread(target=self.log_data)
        self.logging_thread.daemon = True
        self.logging_thread.start()
        print("Grabación de datos CSV iniciada.")
        print("Presiona Ctrl+C en cualquier momento para detener la grabación.\n")
        
        return True
    
    def stop_logging(self):
        """Detiene la grabación de datos CSV."""
        if not self.is_logging:
            print("No se está grabando datos CSV.")
            return
        
        # Enviar comando para detener grabación en el ESP32
        self.send_command("/stop_csv")
        print("Comando de detención enviado al ESP32.")
        
        self.is_logging = False
        if self.logging_thread:
            self.logging_thread.join(1.0)
            self.logging_thread = None
        
        if self.csv_file:
            try:
                self.csv_file.close()
                print(f"\nArchivo CSV cerrado. Se grabaron {self.records_count} registros.")
            except:
                pass
            self.csv_file = None
    
    def log_data(self):
        """Función que se ejecuta en un hilo para capturar y grabar datos."""
        if not self.ser:
            print("Error: No hay conexión serial.")
            self.is_logging = False
            return
        
        # Crear un nuevo archivo CSV con timestamp
        timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        csv_filename = os.path.join(self.output_dir, f"tag_data_{timestamp}.csv")
        
        try:
            self.csv_file = open(csv_filename, 'w')
            print(f"Archivo creado: {csv_filename}")
            
            # Escribir cabecera manualmente para asegurar el formato correcto
            self.csv_file.write("Timestamp,Anchor_ID,Distance_cm,Average_Distance_cm,Signal_Power_dBm,Position_X,Position_Y\n")
            self.csv_file.flush()
            
            header_detected = False
            last_status_update = time.time()
            last_sample_display = time.time()  # Control para mostrar muestras periódicamente
            buffer = ""
            last_sample_lines = []  # Para almacenar las últimas líneas de muestra
            
            # Limpiar buffer al iniciar
            while self.ser.in_waiting > 0:
                self.ser.read(self.ser.in_waiting)
                time.sleep(0.1)
                
            print("Buffer limpiado, esperando datos...")
            print("Activa la grabación CSV en la interfaz web del ESP32 ahora.")
            
            # Patrones para extraer datos de las líneas recibidas
            anchor_pattern = re.compile(r'Anclaje (\d+) (\d+\.\d+) cm, Promedio = (\d+\.\d+), Potencia = ([-]?\d+\.\d+)dBm')
            
            while self.is_logging:
                if self.ser.in_waiting:
                    # Leer bytes y decodificar
                    try:
                        raw_data = self.ser.readline()
                        line = raw_data.decode('utf-8', errors='replace').strip()
                    except Exception as e:
                        if self.debug_mode:
                            print(f"Error leyendo línea: {e}, Bytes: {raw_data}")
                        continue
                    
                    # Modo debug para ver todo lo que llega
                    if self.debug_mode:
                        print(f"DEBUG: {line}")
                    
                    # Extraer datos de los anchors
                    match = anchor_pattern.search(line)
                    if match:
                        try:
                            # Extraer los datos de la línea
                            anchor_id = match.group(1)
                            distance = match.group(2)
                            avg_distance = match.group(3)
                            signal_power = match.group(4)
                            
                            # Obtener posición del anchor
                            position_x, position_y = self.last_positions.get(anchor_id, (0, 0))
                            
                            # Crear línea CSV
                            current_ms = int(time.time() * 1000)  # Timestamp en milisegundos
                            csv_line = f"{current_ms},{anchor_id},{distance},{avg_distance},{signal_power},{position_x},{position_y}"
                            
                            # Escribir al archivo CSV
                            self.csv_file.write(csv_line + '\n')
                            self.csv_file.flush()
                            self.records_count += 1
                            
                            # Crear una versión formateada para mostrar
                            formatted_line = f"Anchor {anchor_id}: Dist={float(distance):.2f}cm, Avg={float(avg_distance):.2f}cm, RSSI={signal_power}dBm, Pos=({position_x},{position_y})"
                            
                            # Actualizar el último dato para este anchor
                            found = False
                            for i, (aid, _) in enumerate(last_sample_lines):
                                if aid == int(anchor_id):
                                    last_sample_lines[i] = (int(anchor_id), formatted_line)
                                    found = True
                                    break
                            
                            if not found and len(last_sample_lines) < 4:
                                last_sample_lines.append((int(anchor_id), formatted_line))
                            
                            # Mostrar progreso periódicamente
                            current_time = time.time()
                            if current_time - last_status_update >= 2:
                                print(f"\rRegistros grabados: {self.records_count}", end="", flush=True)
                                last_status_update = current_time
                            
                            # Mostrar muestras de datos cada 5 segundos
                            if current_time - last_sample_display >= 5:
                                print("\n----- MUESTRA DE DATOS -----")
                                for _, sample in sorted(last_sample_lines):
                                    print(sample)
                                print("---------------------------")
                                last_sample_display = current_time
                            
                        except Exception as e:
                            if self.debug_mode:
                                print(f"Error al procesar dato del anchor: {e}")
                    
                    # Solo mostrar líneas de información si está habilitado el modo verbose
                    elif line and self.verbose_output and not match:
                        # Filtrar información específica que no queremos mostrar
                        if not any(text in line for text in ["Anclaje", "cm, Promedio ="]):
                            print(f"INFO: {line}")
                else:
                    time.sleep(0.01)  # Pequeña pausa para no saturar la CPU
                    
        except Exception as e:
            if self.is_logging:  # Solo mostrar error si todavía estamos en modo de grabación
                print(f"\nError en la grabación: {e}")
        finally:
            if self.csv_file:
                try:
                    self.csv_file.close()
                    if self.is_logging:  # Solo si es una excepción inesperada
                        print(f"\nArchivo CSV cerrado debido a una excepción. Se grabaron {self.records_count} registros.")
                except:
                    pass
                self.csv_file = None
            self.is_logging = False
    
    def send_command(self, command):
        """Envía un comando al ESP32."""
        if not self.is_connected or not self.ser:
            print("No hay conexión al dispositivo.")
            return False
        
        try:
            # Para comandos HTTP, simplemente usamos GET
            if command.startswith('/'):
                url = f"GET {command} HTTP/1.1\r\n\r\n"
                self.ser.write(url.encode())
                return True
            else:
                # Para otros comandos, enviar directamente
                self.ser.write(f"{command}\r\n".encode())
                return True
        except Exception as e:
            print(f"Error al enviar comando: {e}")
            return False

def handle_exit(signal, frame, logger=None):
    """Manejador para salida limpia del programa"""
    print("\nCerrando el programa...")
    if logger and logger.is_logging:
        logger.stop_logging()
    if logger and logger.is_connected:
        logger.disconnect()
    sys.exit(0)

def main():
    parser = argparse.ArgumentParser(description='Registrador de datos CSV para ESP32.')
    parser.add_argument('-p', '--port', help='Puerto serial (COM12, /dev/ttyUSB0, etc.)')
    parser.add_argument('-b', '--baud', type=int, default=115200, help='Velocidad en baudios (default: 115200)')
    parser.add_argument('-o', '--output', default='data_recordings', help='Directorio de salida para archivos CSV')
    parser.add_argument('-v', '--verbose', action='store_true', help='Mostrar mensajes informativos detallados')
    parser.add_argument('-d', '--debug', action='store_true', help='Activar modo de depuración')
    parser.add_argument('--no-commands', action='store_true', help='No enviar comandos al ESP32, solo capturar datos')
    args = parser.parse_args()
    
    logger = CsvLogger(port=args.port, baud_rate=args.baud, output_dir=args.output)
    logger.verbose_output = args.verbose  # Establecer modo verbose según argumento
    logger.debug_mode = args.debug        # Establecer modo debug según argumento
    
    # Registrar manejador de señales para salida limpia
    signal.signal(signal.SIGINT, lambda s, f: handle_exit(s, f, logger))
    
    if not logger.connect():
        print("No se pudo conectar. Saliendo.")
        sys.exit(1)
    
    print("\n=== Registrador de datos CSV para ESP32 ===")
    print("Comandos disponibles:")
    print("  start  - Iniciar grabación CSV")
    print("  stop   - Detener grabación CSV")
    print("  status - Ver estado actual")
    print("  debug  - Activar/desactivar modo debug")
    print("  exit   - Salir del programa")
    print("  Ctrl+C - Detener grabación y/o salir")
    
    # Si se especifica --no-commands, iniciar grabación automáticamente
    if args.no_commands:
        print("\nModo pasivo activado. Solo se capturarán datos sin enviar comandos al ESP32.")
        print("Iniciando captura de datos automáticamente...")
        logger.is_logging = True
        logger.records_count = 0
        logger.logging_thread = threading.Thread(target=logger.log_data)
        logger.logging_thread.daemon = True
        logger.logging_thread.start()
        print("Captura de datos iniciada. Presiona Ctrl+C para detener.")
    
    try:
        while True:
            cmd = input("\nComando> ").strip().lower()
            
            if cmd == "start":
                if args.no_commands:
                    print("En modo pasivo. Solo capturando datos sin enviar comandos.")
                else:
                    logger.start_logging()
            elif cmd == "stop":
                if args.no_commands:
                    print("En modo pasivo. Para detener, use Ctrl+C.")
                else:
                    logger.stop_logging()
            elif cmd == "status":
                status = "Conectado" if logger.is_connected else "Desconectado"
                status += ", Grabando" if logger.is_logging else ", No grabando"
                print(f"Estado: {status}")
                if logger.records_count > 0:
                    print(f"Registros grabados: {logger.records_count}")
            elif cmd == "debug":
                logger.debug_mode = not logger.debug_mode
                print(f"Modo debug: {'Activado' if logger.debug_mode else 'Desactivado'}")
            elif cmd == "exit":
                break
            elif cmd == "":
                # Para evitar confusión con líneas en blanco
                pass
            else:
                print("Comando no reconocido.")
    except KeyboardInterrupt:
        print("\nPrograma interrumpido.")
    finally:
        handle_exit(None, None, logger)

if __name__ == "__main__":
    main()
