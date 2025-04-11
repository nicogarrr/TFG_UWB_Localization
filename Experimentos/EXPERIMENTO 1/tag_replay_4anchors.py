import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import matplotlib.image as mpimg
import os
import json
from tkinter import Tk, filedialog
from scipy.optimize import minimize
import datetime
import time

class TagReplay:
    def __init__(self):
        # Configuración del espacio experimental (3.45m x 5.1m)
        self.field_length = 5.1   # metros (largo)
        self.field_width = 3.45   # metros (ancho)
        
        # Posiciones de los anchors (x, y, z) en metros según los datos MEMORY
        self.anchors = {
            10: {'position': [0.0, 1.10, 0.0], 'color': 'red', 'label': 'Anchor 10'},
            20: {'position': [0.0, 4.55, 0.0], 'color': 'green', 'label': 'Anchor 20'},
            30: {'position': [3.45, 3.5, 0.0], 'color': 'blue', 'label': 'Anchor 30'},
            40: {'position': [3.45, 0.66, 0.0], 'color': 'purple', 'label': 'Anchor 40'}
        }
        
        self.data = None
        self.animation = None
        self.current_frame = 0
        self.total_frames = 0
        self.playing = False
        self.play_speed = 1.0
        
        # Cargar posiciones guardadas de anchors si existen
        self.config_file = 'anchor_positions.json'
        self.load_anchor_positions()
    
    def load_anchor_positions(self):
        """Carga las posiciones de los anchors desde un archivo de configuración."""
        if os.path.exists(self.config_file):
            try:
                with open(self.config_file, 'r') as f:
                    config = json.load(f)
                    for anchor_id, data in config.items():
                        if int(anchor_id) in self.anchors:
                            self.anchors[int(anchor_id)]['position'] = data['position']
                print(f"Posiciones de anchors cargadas desde {self.config_file}")
            except Exception as e:
                print(f"Error al cargar posiciones de anchors: {e}")
    
    def save_anchor_positions(self):
        """Guarda las posiciones de los anchors en un archivo de configuración."""
        try:
            config = {}
            for anchor_id, data in self.anchors.items():
                config[str(anchor_id)] = {'position': data['position']}
            
            with open(self.config_file, 'w') as f:
                json.dump(config, f, indent=2)
            print(f"Posiciones de anchors guardadas en {self.config_file}")
        except Exception as e:
            print(f"Error al guardar posiciones de anchors: {e}")
    
    def setup_anchors(self):
        """Configura las posiciones de los anchors mediante entrada del usuario."""
        print("\nConfiguración de posiciones de anchors en el espacio experimental (en metros)")
        print(f"Dimensiones del espacio: {self.field_length}x{self.field_width} metros")
        print("Formato: x y (ejemplo: 1.5 2.0)")
        
        for anchor_id in self.anchors:
            current_pos = self.anchors[anchor_id]['position']
            print(f"\nAnchor {anchor_id} - Posición actual: [{current_pos[0]}, {current_pos[1]}]")
            pos_input = input(f"Nueva posición para Anchor {anchor_id} (Enter para mantener actual): ")
            
            if pos_input.strip():
                try:
                    x, y = map(float, pos_input.split())
                    # Validar que las coordenadas estén dentro del espacio
                    x = max(0, min(x, self.field_width))
                    y = max(0, min(y, self.field_length))
                    self.anchors[anchor_id]['position'] = [x, y, 0.0]
                    print(f"Posición actualizada: [{x}, {y}, 0.0]")
                except ValueError:
                    print("Formato inválido. Manteniendo la posición actual.")
        
        self.save_anchor_positions()
        
    def select_csv_file(self):
        """Abre un diálogo para seleccionar un archivo CSV de datos."""
        root = Tk()
        root.withdraw()  # Ocultar la ventana principal
        
        # Directorio inicial para el diálogo
        initial_dir = os.path.join(os.getcwd(), "data_recordings")
        if not os.path.exists(initial_dir):
            initial_dir = os.getcwd()
        
        # Abrir diálogo para seleccionar archivo
        file_path = filedialog.askopenfilename(
            title="Seleccione un archivo CSV de datos",
            initialdir=initial_dir,
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")]
        )
        
        root.destroy()
        
        if file_path:
            print(f"Archivo seleccionado: {file_path}")
            return file_path
        else:
            print("No se seleccionó ningún archivo.")
            return None
    
    def load_data(self, csv_file=None):
        """Carga los datos desde un archivo CSV."""
        if not csv_file:
            csv_file = self.select_csv_file()
            if not csv_file:
                return False
        
        try:
            # Cargar datos
            self.data = pd.read_csv(csv_file)
            print(f"Columnas en CSV: {self.data.columns.tolist()}")
            print(f"Primeras filas del CSV:")
            print(self.data.head())
            
            # Verificar que el archivo tenga la estructura esperada
            expected_columns = ['Timestamp', 'Anchor_ID', 'Distance_cm', 'Average_Distance_cm', 'Signal_Power_dBm']
            
            # Verificar si tenemos las columnas necesarias
            has_required_format = all(col in self.data.columns for col in expected_columns)
            if not has_required_format:
                print(f"Error: El archivo CSV no tiene la estructura esperada.")
                print(f"Se esperaban las columnas: {expected_columns}")
                print(f"Columnas encontradas: {self.data.columns.tolist()}")
                return False
            
            # IGNORAR las columnas de posición en el CSV - calcular siempre por trilateración
            print("IGNORANDO columnas Position_X/Y en el CSV - calculando posiciones por trilateración")
            has_position = False
            
            # Crear tiempo relativo
            if 'RelativeTime' not in self.data.columns:
                if 'Timestamp' in self.data.columns:
                    try:
                        # Si es un timestamp en texto, convertir a datetime
                        if self.data['Timestamp'].dtype == 'object':
                            try:
                                self.data['Timestamp'] = pd.to_datetime(self.data['Timestamp'])
                                start_time = self.data['Timestamp'].min()
                                self.data['RelativeTime'] = (self.data['Timestamp'] - start_time).dt.total_seconds()
                                print("Tiempo relativo calculado desde timestamps")
                            except:
                                print("No se pudo convertir el timestamp a datetime, usando índices")
                                self.data['RelativeTime'] = range(len(self.data))
                        # Si es numérico, asumir que son milisegundos desde el inicio
                        else:
                            min_time = self.data['Timestamp'].min()
                            self.data['RelativeTime'] = (self.data['Timestamp'] - min_time) / 1000.0
                            print("Tiempo relativo calculado desde timestamps numéricos")
                    except Exception as e:
                        print(f"Error al procesar timestamps: {e}")
                        self.data['RelativeTime'] = range(len(self.data))
                else:
                    print("No hay columna Timestamp, usando índices")
                    self.data['RelativeTime'] = range(len(self.data))
            
            # Filtrar datos inválidos
            self.data = self.data[self.data['Distance_cm'] > 0]
            self.data = self.data[self.data['Distance_cm'] < 10000]  # 100 metros máximo
            
            # Agrupar por instantes de tiempo
            time_step = 0.1  # segundos
            self.data['TimeGroup'] = (self.data['RelativeTime'] / time_step).astype(int)
            
            # Obtener grupos de tiempo únicos
            unique_time_groups = sorted(self.data['TimeGroup'].unique())
            
            # Calcular posiciones para cada grupo de tiempo mediante trilateración
            print("Calculando posiciones por trilateración...")
            positions = []
            for group in unique_time_groups:
                group_data = self.data[self.data['TimeGroup'] == group]
                
                # Extraer distancias a cada anchor
                distances = {}
                for anchor_id in self.anchors.keys():
                    anchor_data = group_data[group_data['Anchor_ID'] == anchor_id]
                    if not anchor_data.empty:
                        # Convertir cm a metros
                        distances[int(anchor_id)] = anchor_data['Average_Distance_cm'].iloc[0] / 100.0
                
                # Si no hay suficientes distancias, pasar al siguiente grupo
                if len(distances) < 3:
                    continue
                
                # Calcular posición mediante trilateración
                tag_position = self.trilateration_2d(distances)
                
                # Si se calculó una posición válida, añadirla
                if tag_position is not None:
                    # Limitar valores a las dimensiones del campo
                    tag_position[0] = max(0, min(tag_position[0], self.field_width))
                    tag_position[1] = max(0, min(tag_position[1], self.field_length))
                    
                    positions.append({
                        'time': group_data['RelativeTime'].mean(),
                        'position': tag_position,
                        'distances': distances
                    })
            
            self.positions = positions
            self.total_frames = len(positions)
            
            # Verificar si se pudieron calcular posiciones
            if self.total_frames == 0:
                print("No se pudieron calcular posiciones con los datos proporcionados.")
                return False
            
            # Depuración: Mostrar primeras posiciones calculadas
            print(f"\nPosiciones calculadas: {self.total_frames}")
            for i in range(min(10, self.total_frames)):
                print(f"  {i}: Tiempo={positions[i]['time']:.2f}s, Pos=({positions[i]['position'][0]:.2f}, {positions[i]['position'][1]:.2f})")
            
            return True
            
        except Exception as e:
            print(f"Error al cargar datos: {e}")
            import traceback
            traceback.print_exc()
            return False
    
    def trilateration_2d(self, distances):
        """Calcula la posición 2D del tag usando trilateración optimizada para 4 anchors."""
        if len(distances) < 3:
            return None  # Necesitamos al menos 3 anchors para trilateración
        
        # Función a minimizar: suma de las diferencias cuadráticas entre
        # las distancias medidas y las distancias calculadas
        def error_function(point):
            x, y = point
            error_sum = 0
            
            for anchor_id, measured_distance in distances.items():
                if anchor_id in self.anchors:
                    anchor_pos = self.anchors[anchor_id]['position']
                    # Calcular distancia euclidiana
                    calculated_distance = np.sqrt((x - anchor_pos[0])**2 + (y - anchor_pos[1])**2)
                    # Sumar error cuadrático
                    error_sum += (calculated_distance - measured_distance)**2
            
            return error_sum
        
        # Punto inicial para la optimización (centro del área)
        initial_guess = [self.field_width/2, self.field_length/2]
        
        # Restricciones para mantener la posición dentro del área
        bounds = [(0, self.field_width), (0, self.field_length)]
        
        # Optimización para encontrar la posición que minimiza el error
        result = minimize(error_function, initial_guess, bounds=bounds, method='L-BFGS-B')
        
        if result.success:
            return result.x.tolist()
        else:
            print("No se pudo calcular la posición mediante trilateración.")
            return initial_guess  # Devolver posición inicial como fallback
    
    def create_visualization(self):
        """Crea la visualización y la animación."""
        
        # Configuración de la figura
        plt.close('all')  # Cerrar figuras anteriores
        plt.ioff()  # Desactivar modo interactivo durante la creación
        
        self.fig, self.ax = plt.subplots(figsize=(12, 9))
        self.fig.canvas.manager.set_window_title('Visualizador de Tag UWB')
        
        # Configurar límites del área con espacio adicional para visibilidad
        margin = 0.5  # margen en metros alrededor del área
        self.ax.set_xlim(-margin, self.field_width + margin)
        self.ax.set_ylim(-margin, self.field_length + margin)
        
        # Dibujar el contorno del área experimental
        rect = plt.Rectangle((0, 0), self.field_width, self.field_length, 
                            fill=False, linestyle='-', linewidth=2, color='gray')
        self.ax.add_patch(rect)
        
        # Configurar estética
        self.ax.set_xlabel('X (metros)', fontsize=12)
        self.ax.set_ylabel('Y (metros)', fontsize=12)
        self.ax.set_title(f'Posicionamiento UWB - Espacio {self.field_width}m x {self.field_length}m', fontsize=14)
        self.ax.grid(True, linestyle='--', alpha=0.7)
        
        # Dibujar anchors
        for anchor_id, data in self.anchors.items():
            x, y = data['position'][0], data['position'][1]
            self.ax.plot(x, y, 'o', color=data['color'], markersize=12, label=data['label'])
            self.ax.text(x, y, f" {anchor_id}", fontsize=12, weight='bold', 
                         verticalalignment='bottom', horizontalalignment='left')
        
        # Obtener la primera posición para inicializar
        if self.total_frames > 0:
            init_x, init_y = self.positions[0]['position']
            print(f"Posición inicial del tag: ({init_x:.2f}, {init_y:.2f})")
        else:
            init_x, init_y = self.field_width/2, self.field_length/2
            print("No hay posiciones calculadas, usando centro del área")
        
        # Crear marcador para la posición actual - MUCHO más visible
        self.position_marker, = self.ax.plot([init_x], [init_y], 'o', 
                                            markersize=15, color='red', 
                                            markerfacecolor='red',
                                            markeredgecolor='black',
                                            markeredgewidth=2,
                                            zorder=100)  # Asegurar que esté por encima de todo
        
        # Crear línea para la trayectoria
        self.trajectory_line, = self.ax.plot([init_x], [init_y], '-', 
                                            color='orange', linewidth=2.5, alpha=0.8,
                                            zorder=90)  # Alto zorder para que sea visible
        
        # Añadir leyenda en posición óptima
        self.ax.legend(loc='upper left', bbox_to_anchor=(0.02, 0.98))
        
        # Añadir información de tiempo - más visible
        self.time_text = self.ax.text(0.02, 0.02, 'Tiempo: 0.00 s', 
                                      transform=self.ax.transAxes, fontsize=14,
                                      verticalalignment='bottom', 
                                      bbox=dict(boxstyle='round', facecolor='wheat', 
                                              alpha=0.8, edgecolor='brown', pad=0.5))
        
        # Crear círculos de distancia para cada anchor
        self.distance_circles = {}
        for anchor_id, data in self.anchors.items():
            circle = plt.Circle((data['position'][0], data['position'][1]), 0, 
                               fill=False, linestyle='--', alpha=0.5, 
                               color=data['color'], linewidth=1.5)
            self.distance_circles[anchor_id] = circle
            self.ax.add_patch(circle)
        
        # Ajustar márgenes y establecer aspecto igual para dimensiones correctas
        plt.subplots_adjust(left=0.1, right=0.9, top=0.9, bottom=0.15)
        self.ax.set_aspect('equal')
        
        # Añadir controles de forma más visible
        axcolor = 'lightgoldenrodyellow'
        play_ax = plt.axes([0.78, 0.04, 0.1, 0.05])
        self.play_button = plt.Button(play_ax, 'Play/Pause', color=axcolor)
        self.play_button.on_clicked(self.toggle_play)
        
        reset_ax = plt.axes([0.89, 0.04, 0.1, 0.05])
        self.reset_button = plt.Button(reset_ax, 'Reset', color=axcolor)
        self.reset_button.on_clicked(self.reset_animation)
        
        # Asegurar que el marcador del tag sea visible inicialmente
        self.update(0)
        plt.draw()
        print("Visualización inicializada, tag debería ser visible en posición inicial")
        
        # Iniciar la animación con intervalo más corto para fluidez
        self.animation = FuncAnimation(
            self.fig, 
            self.update, 
            frames=range(self.total_frames),
            interval=50,  # 50ms para animación fluida
            blit=False,  # Desactivar blit para forzar redibujado completo
            repeat=True
        )
    
    def update(self, frame):
        """Actualiza la animación para el cuadro actual."""
        # Gestión del índice de frames
        if isinstance(frame, int) and frame < self.total_frames:
            if self.playing or frame == 0:
                self.current_frame = frame
            else:
                frame = self.current_frame
        else:
            # Gestión alternativa si frame no es un entero o está fuera de rango
            if self.playing:
                self.current_frame = (self.current_frame + 1) % self.total_frames
            frame = self.current_frame
        
        # Si hemos llegado al final, volver al inicio
        if frame >= self.total_frames:
            self.current_frame = 0
            frame = 0
        
        # Obtener datos del cuadro actual
        position_data = self.positions[frame]
        x, y = position_data['position']
        
        # Debug: mostrar posición actual
        if frame % 10 == 0:  # Mostrar cada 10 frames para no saturar la consola
            print(f"Frame {frame}: Posición tag = ({x:.2f}, {y:.2f})")
        
        # Actualizar marcador de posición
        self.position_marker.set_data([x], [y])
        
        # Recolectar todos los puntos hasta el cuadro actual para la trayectoria
        traj_x = [self.positions[i]['position'][0] for i in range(frame + 1)]
        traj_y = [self.positions[i]['position'][1] for i in range(frame + 1)]
        self.trajectory_line.set_data(traj_x, traj_y)
        
        # Actualizar círculos de distancia
        for anchor_id, circle in self.distance_circles.items():
            if anchor_id in position_data['distances']:
                distance = position_data['distances'][anchor_id]
                circle.set_radius(distance)
                circle.set_visible(True)
            else:
                circle.set_visible(False)
        
        # Actualizar texto de tiempo
        elapsed_time = position_data['time']
        self.time_text.set_text(f'Tiempo: {elapsed_time:.2f} s')
        
        # Refrescar la vista
        self.fig.canvas.draw_idle()
        
        return [self.position_marker, self.trajectory_line, self.time_text] + list(self.distance_circles.values())
    
    def toggle_play(self, event):
        """Alterna entre reproducir y pausar la animación."""
        self.playing = not self.playing
        if self.playing:
            print("Reproduciendo...")
        else:
            print("Pausado.")

    def reset_animation(self, event):
        """Reinicia la animación al principio."""
        self.current_frame = 0
        print("Animación reiniciada.")
        self.update(0)  # Actualizar visualización al frame inicial
        plt.draw()      # Refrescar la visualización
    
    def generate_heatmap(self):
        """Genera un mapa de calor de las posiciones del tag."""
        if not hasattr(self, 'positions') or len(self.positions) == 0:
            print("No hay datos cargados para generar el mapa de calor.")
            return
        
        # Extraer todas las posiciones
        positions = np.array([p['position'] for p in self.positions])
        
        # Crear figura
        plt.figure(figsize=(10, 8))
        
        # Configurar rejilla para el mapa de calor
        grid_size = 50
        x_grid = np.linspace(0, self.field_width, grid_size)
        y_grid = np.linspace(0, self.field_length, grid_size)
        grid = np.zeros((grid_size, grid_size))
        
        # Calcular densidad de posiciones
        for pos in positions:
            x_idx = int(pos[0] / self.field_width * (grid_size - 1))
            y_idx = int(pos[1] / self.field_length * (grid_size - 1))
            x_idx = max(0, min(x_idx, grid_size - 1))
            y_idx = max(0, min(y_idx, grid_size - 1))
            grid[y_idx, x_idx] += 1
        
        # Suavizar el mapa de calor
        from scipy.ndimage import gaussian_filter
        grid = gaussian_filter(grid, sigma=1.5)
        
        # Dibujar mapa de calor
        plt.imshow(grid, extent=[0, self.field_width, 0, self.field_length], 
                  origin='lower', cmap='hot', interpolation='bilinear')
        
        plt.colorbar(label='Densidad de posiciones')
        
        # Dibujar anchors
        for anchor_id, data in self.anchors.items():
            x, y = data['position'][0], data['position'][1]
            plt.plot(x, y, 'o', color=data['color'], markersize=10)
            plt.text(x, y, f" {anchor_id}", fontsize=10, verticalalignment='bottom')
        
        # Configuración adicional
        plt.title('Mapa de calor de posiciones')
        plt.xlabel('X (metros)')
        plt.ylabel('Y (metros)')
        plt.grid(True, linestyle='--', alpha=0.3)
        
        plt.tight_layout()
        plt.show()
    
    def calculate_statistics(self):
        """Calcula estadísticas sobre los datos cargados."""
        if not hasattr(self, 'positions') or len(self.positions) == 0:
            print("No hay datos cargados para calcular estadísticas.")
            return
        
        print("\n=== Estadísticas de la grabación ===")
        
        # Duración total
        duration = self.positions[-1]['time'] - self.positions[0]['time']
        print(f"Duración total: {duration:.2f} segundos")
        
        # Distancia recorrida
        total_distance = 0
        prev_pos = None
        for position_data in self.positions:
            pos = position_data['position']
            if prev_pos is not None:
                segment_distance = np.sqrt((pos[0] - prev_pos[0])**2 + (pos[1] - prev_pos[1])**2)
                total_distance += segment_distance
            prev_pos = pos
        
        print(f"Distancia total recorrida: {total_distance:.2f} metros")
        print(f"Velocidad media: {total_distance / duration * 3.6:.2f} km/h")
        
        # Calcular área cubierta (convex hull)
        try:
            from scipy.spatial import ConvexHull
            positions = np.array([p['position'] for p in self.positions])
            hull = ConvexHull(positions)
            print(f"Área aproximada cubierta: {hull.volume:.2f} m²")
        except:
            print("No se pudo calcular el área cubierta.")
        
        # Precisión por anclaje (promedio de desviación estándar)
        anchor_stats = {}
        for anchor_id in self.anchors.keys():
            distances = [p['distances'].get(anchor_id, np.nan) for p in self.positions]
            distances = [d for d in distances if not np.isnan(d)]
            if distances:
                mean_dist = np.mean(distances)
                std_dist = np.std(distances)
                anchor_stats[anchor_id] = {
                    'mean': mean_dist,
                    'std': std_dist,
                    'count': len(distances)
                }
        
        print("\nEstadísticas por anclaje:")
        for anchor_id, stats in anchor_stats.items():
            print(f"Anchor {anchor_id}: {stats['count']} mediciones, "
                  f"Distancia media: {stats['mean']:.2f}m, "
                  f"Desviación: {stats['std']:.2f}m")
                
        return {
            'duration': duration,
            'total_distance': total_distance,
            'average_speed': total_distance / duration,
            'anchor_stats': anchor_stats
        }
    
    def run(self):
        """Ejecuta el visor completo con funcionalidad interactiva."""
        # Cargar datos directamente desde una ruta específica
        csv_file = r"C:\Users\nicoi\OneDrive\Escritorio\ESP32\data_recordings\tag_data_20250326_014434.csv"
        print(f"Cargando datos desde: {csv_file}")
        
        if self.load_data(csv_file):
            # Crear la visualización y la animación
            self.create_visualization()
            
            # Mostrar estadísticas
            self.calculate_statistics()
            
            # Activar reproducción inmediata
            self.playing = True
            print("Iniciando reproducción automática...")
            
            # Mantener referencia a la animación y mostrar
            try:
                plt.show(block=True)
            except KeyboardInterrupt:
                print("Visualización interrumpida por el usuario")
            
            # Forzar cierre de todas las figuras al salir
            plt.close('all')
            
            # Mostrar el mapa de calor solo si se cerró la animación normalmente
            print("\nGenerando mapa de calor...")
            self.generate_heatmap()
            plt.show()
        else:
            print("No se pudieron cargar los datos. Saliendo.")
    
    def toggle_play(self, event):
        """Alterna entre reproducir y pausar la animación."""
        self.playing = not self.playing
        if self.playing:
            print("Reproduciendo...")
        else:
            print("Pausado.")

    def reset_animation(self, event):
        """Reinicia la animación al principio."""
        self.current_frame = 0
        print("Animación reiniciada.")
        self.update(0)  # Actualizar visualización al frame inicial
        plt.draw()      # Refrescar la visualización

if __name__ == "__main__":
    replay = TagReplay()
    replay.run()
