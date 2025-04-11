import serial
import time
import csv
from datetime import datetime
import os
import threading

class TagController:
    def __init__(self, port=None, baud_rate=2000000):
        # Si no se especifica un puerto, intentar encontrar uno automáticamente
        self.port = port
        self.baud_rate = baud_rate
        self.ser = None
        self.is_connected = False
        self.is_reading = False
        self.read_thread = None
        self.data_folder = "data_recordings"
        self.current_file = None
        self.writer = None
        self.csv_file = None
        self.recording = False
        
        # Atributos para compartir datos con el visualizador
        self.anchor_avg = {}
        self.anchor_distance = {}
        self.pot_sig = {}
        
        # Crear carpeta de datos si no existe
        if not os.path.exists(self.data_folder):
            os.makedirs(self.data_folder)
    
    def list_serial_ports(self):
        """Lista los puertos seriales disponibles."""
        import serial.tools.list_ports
        ports = serial.tools.list_ports.comports()
        available_ports = [p.device for p in ports]
        return available_ports
    
    def connect(self, port=None):
        """Conecta al dispositivo tag."""
        if port:
            self.port = port
        
        if not self.port:
            # Intentar encontrar el puerto automáticamente
            ports = self.list_serial_ports()
            if not ports:
                print("No se encontraron puertos seriales disponibles.")
                return False
            self.port = ports[0]  # Usar el primer puerto disponible
        
        try:
            self.ser = serial.Serial(self.port, self.baud_rate, timeout=1)
            self.is_connected = True
            print(f"Conectado al puerto {self.port} a {self.baud_rate} baudios")
            return True
        except Exception as e:
            print(f"Error al conectar: {e}")
            self.is_connected = False
            return False
    
    def disconnect(self):
        """Desconecta del dispositivo tag."""
        if self.is_reading:
            self.stop_reading()
        
        if self.ser and self.is_connected:
            self.ser.close()
            self.is_connected = False
            print(f"Desconectado del puerto {self.port}")
    
    def start_recording(self, filename=None):
        """Inicia la grabación de datos en un archivo CSV."""
        if not self.is_connected:
            print("No hay conexión al dispositivo.")
            return False
        
        if not filename:
            # Crear nombre de archivo con fecha y hora actual
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"tag_data_{timestamp}.csv"
        
        try:
            self.current_file = os.path.join(self.data_folder, filename)
            self.csv_file = open(self.current_file, 'w', newline='', encoding='utf-8')
            self.writer = csv.writer(self.csv_file)
            
            # Escribir encabezado
            self.writer.writerow(['Timestamp', 'Anchor_ID', 'Distance_cm', 'Average_Distance_cm', 'Signal_Power_dBm'])
            self.recording = True
            print(f"Grabando datos en {self.current_file}")
            return True
        except Exception as e:
            print(f"Error al iniciar la grabación: {e}")
            return False
    
    def stop_recording(self):
        """Detiene la grabación de datos."""
        if self.csv_file:
            self.csv_file.close()
            self.csv_file = None
            self.writer = None
            self.recording = False
            print(f"Grabación detenida. Datos guardados en {self.current_file}")
    
    def read_line(self):
        """Lee una línea de datos del dispositivo tag."""
        if not self.is_connected or not self.ser:
            print("No hay conexión al dispositivo.")
            return None
        
        try:
            line = self.ser.readline().decode('utf-8').strip()
            if line:
                return line
            return None
        except Exception as e:
            print(f"Error al leer datos: {e}")
            return None
    
    def parse_data(self, line):
        """Analiza una línea de datos del dispositivo tag."""
        # Formato esperado: "Anclaje {ID} {distancia} cm, Promedio = {valor}, Potencia = {valor}dBm"
        try:
            # Extraer ID del anclaje
            anchor_id = int(line.split('Anclaje ')[1].split(' ')[0])
            
            # Extraer distancia
            distance_cm = float(line.split(' ')[2].replace(',', '.'))
            
            # Extraer promedio
            avg_distance = float(line.split('Promedio = ')[1].split(',')[0].replace(',', '.'))
            
            # Extraer potencia de señal
            signal_power = float(line.split('Potencia = ')[1].split('dBm')[0].replace(',', '.'))
            
            # Almacenar valores en atributos para que los use el visualizador
            self.anchor_distance[anchor_id] = distance_cm
            self.anchor_avg[anchor_id] = avg_distance
            self.pot_sig[anchor_id] = signal_power
            
            return {
                'timestamp': datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f"),
                'anchor_id': anchor_id,
                'distance_cm': distance_cm,
                'avg_distance_cm': avg_distance,
                'signal_power_dBm': signal_power
            }
        except Exception as e:
            print(f"Error al analizar datos: {e}\nLínea: {line}")
            return None
    
    def read_and_process(self):
        """Lee y procesa continuamente datos del dispositivo tag."""
        while self.is_reading:
            line = self.read_line()
            if line:
                print(line)
                
                # Si la línea contiene información de un anclaje, procesarla
                if "Anclaje" in line:
                    data = self.parse_data(line)
                    if data and self.recording and self.writer:
                        self.writer.writerow([
                            data['timestamp'],
                            data['anchor_id'],
                            data['distance_cm'],
                            data['avg_distance_cm'],
                            data['signal_power_dBm']
                        ])
                        self.csv_file.flush()  # Asegurar que los datos se escriban inmediatamente
            
            time.sleep(0.01)  # Pequeña pausa para no sobrecargar la CPU
    
    def start_reading(self):
        """Inicia la lectura continua de datos en un hilo separado."""
        if not self.is_connected:
            print("No hay conexión al dispositivo.")
            return False
        
        if self.is_reading:
            print("Ya se está leyendo datos.")
            return True
        
        self.is_reading = True
        self.read_thread = threading.Thread(target=self.read_and_process)
        self.read_thread.daemon = True  # El hilo se cerrará cuando el programa principal termine
        self.read_thread.start()
        print("Lectura de datos iniciada.")
        return True
    
    def stop_reading(self):
        """Detiene la lectura continua de datos."""
        if not self.is_reading:
            print("No se está leyendo datos.")
            return
        
        self.is_reading = False
        if self.read_thread:
            self.read_thread.join(1.0)  # Esperar a que el hilo termine (timeout de 1 segundo)
            self.read_thread = None
        print("Lectura de datos detenida.")
    
    def send_command(self, command):
        """Envía un comando al dispositivo tag."""
        if not self.is_connected or not self.ser:
            print("No hay conexión al dispositivo.")
            return False
        
        try:
            self.ser.write((command + '\n').encode('utf-8'))
            return True
        except Exception as e:
            print(f"Error al enviar comando: {e}")
            return False


if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description='Control de dispositivo tag UWB')
    parser.add_argument('--port', type=str, help='Puerto serial (ejemplo: COM3)')
    parser.add_argument('--baud', type=int, default=2000000, help='Velocidad en baudios (predeterminado: 2000000)')
    parser.add_argument('--record', action='store_true', help='Iniciar grabación de datos inmediatamente')
    args = parser.parse_args()
    
    controller = TagController(port=args.port, baud_rate=args.baud)
    
    try:
        # Mostrar puertos disponibles si no se especificó uno
        if not args.port:
            success = False
            while not success:
                ports = controller.list_serial_ports()
                if ports:
                    print("Puertos seriales disponibles:")
                    for i, port in enumerate(ports):
                        print(f"{i+1}. {port}")
                    choice = input("Seleccione un puerto (número) o presione Enter para usar el primero: ")
                    if choice.strip():
                        selected_port = ports[int(choice)-1]
                    else:
                        selected_port = ports[0]
                    print(f"Usando puerto: {selected_port}")
                    success = controller.connect(selected_port)
                    if not success:
                        retry = input("¿Desea intentar con otro puerto? (s/n): ").strip().lower()
                        if retry != 's':
                            print("Programa finalizado.")
                            exit(1)
                else:
                    print("No se encontraron puertos seriales disponibles.")
                    exit(1)
        else:
            success = controller.connect()
            if not success:
                print("Programa finalizado.")
                exit(1)
        
        # Iniciar grabación si se solicitó
        if args.record:
            controller.start_recording()
        
        # Iniciar lectura continua
        controller.start_reading()
        
        print("\nComandos disponibles:")
        print("  r - Iniciar/detener grabación de datos")
        print("  q - Salir del programa")
        
        while True:
            cmd = input("\nIngrese un comando (r=grabar/parar, q=salir): ").strip().lower()
            
            if cmd == 'q':
                break
            elif cmd == 'r':
                if controller.recording:
                    controller.stop_recording()
                else:
                    controller.start_recording()
            else:
                print(f"Comando desconocido: {cmd}")
    
    except KeyboardInterrupt:
        print("\nPrograma interrumpido por el usuario.")
    
    finally:
        # Asegurarse de cerrar todo correctamente
        if controller.recording:
            controller.stop_recording()
        controller.disconnect()
        print("Programa finalizado.")
