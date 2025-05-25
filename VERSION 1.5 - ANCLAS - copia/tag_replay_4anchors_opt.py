import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import matplotlib.image as mpimg
import os
import json
from tkinter import Tk, filedialog
import datetime
import time
from collections import deque # Para la trayectoria

class TagReplay:
    def __init__(self):
        # Configuración del espacio experimental (3.45m x 5.1m)
        self.field_length = 5.1   # metros (largo)
        self.field_width = 3.45   # metros (ancho)
        self.anchor_height = 1.5 # Added: Common anchor height
        self.trail_length = 50 # Número de puntos para el rastro
        
        # Posiciones de los anchors (x, y, z) en metros
        # Updated Z to self.anchor_height
        self.anchors = {
            10: {'position': [0.0, 1.10, self.anchor_height], 'color': 'red', 'label': 'Anchor 10'},
            20: {'position': [0.0, 4.55, self.anchor_height], 'color': 'green', 'label': 'Anchor 20'},
            30: {'position': [3.45, 3.5, self.anchor_height], 'color': 'blue', 'label': 'Anchor 30'},
            40: {'position': [3.45, 0.66, self.anchor_height], 'color': 'purple', 'label': 'Anchor 40'}
        }
        
        self.data = None
        self.all_data = {} # Store data per tag_id (ahora del archivo procesado)
        self.animation = None
        self.fig = None
        self.ax = None
        self.tag_plots = {}
        self.anchor_plots = {}
        self.radius_circles = {}
        self.tag_trails = {} # Para guardar los rastros
        self.trail_plots = {} # Para los plots de los rastros
        self.info_text = None
        self.time_text = None
        self.current_frame = 0
        self.total_frames = 0
        self.playing = False
        self.play_speed = 1.0 
        self.tag_ids_available = []
        self.selected_tag_id = None # Track which tag is being displayed

        # Cargar posiciones guardadas de anchors si existen
        self.config_file = 'anchor_positions.json'
        self.load_anchor_positions() 
    
    def load_anchor_positions(self):
        """Carga las posiciones de los anchors desde un archivo de configuración."""
        if os.path.exists(self.config_file):
            try:
                with open(self.config_file, 'r') as f:
                    config = json.load(f)
                    for anchor_id_str, data in config.items():
                        anchor_id = int(anchor_id_str)
                        if anchor_id in self.anchors:
                            # Ensure position includes Z, default to self.anchor_height if missing
                            pos = data.get('position', [0, 0, self.anchor_height])
                            if len(pos) == 2:
                                pos.append(self.anchor_height) # Add Z if only X,Y saved
                            self.anchors[anchor_id]['position'] = pos[:3] # Take only X,Y,Z
                print(f"Posiciones de anchors cargadas desde {self.config_file}")
            except Exception as e:
                print(f"Error al cargar posiciones de anchors: {e}. Usando predeterminadas.")
                # Re-initialize Z if loading failed
                for anchor_id in self.anchors:
                    self.anchors[anchor_id]['position'][2] = self.anchor_height

        # Asegurarnos de que las posiciones Z se cargan o se establecen correctamente
        for aid, data in self.anchors.items():
            if len(data['position']) < 3:
                 print(f"Completando Z para anchor {aid} a {self.anchor_height}")
                 self.anchors[aid]['position'] = data['position'][:2] + [self.anchor_height]
            elif len(data['position']) > 3:
                 self.anchors[aid]['position'] = data['position'][:3]

    def save_anchor_positions(self):
        """Guarda las posiciones de los anchors en un archivo de configuración."""
        try:
            config = {}
            for anchor_id, data in self.anchors.items():
                config[str(anchor_id)] = {'position': data['position'][:3]} # Save X,Y,Z
            
            with open(self.config_file, 'w') as f:
                json.dump(config, f, indent=2)
        except Exception as e:
            print(f"Error al guardar posiciones de anchors: {e}")

    def setup_anchors(self):
        """Configura las posiciones de los anchors mediante entrada del usuario."""
        print("\nConfiguración de posiciones de anchors en el espacio experimental (en metros)")
        print(f"Dimensiones del espacio: {self.field_length}m x {self.field_width}m")
        print(f"Altura común de anchors actual: {self.anchor_height}m")
        height_input = input(f"Nueva altura común para todos los anchors (Enter para mantener {self.anchor_height}m): ")
        if height_input.strip():
            try:
                self.anchor_height = float(height_input)
                print(f"Altura común actualizada a: {self.anchor_height}m")
            except ValueError:
                print("Entrada inválida. Manteniendo altura actual.")

        print("Posiciones X, Y. Formato: x y (ejemplo: 1.5 2.0)")
        for anchor_id in self.anchors:
            current_pos = self.anchors[anchor_id]['position']
            print(f"\nAnchor {anchor_id} - Posición actual: [{current_pos[0]:.2f}, {current_pos[1]:.2f}, {self.anchor_height:.2f}]")
            pos_input = input(f"Nuevas coordenadas X Y para Anchor {anchor_id} (Enter para mantener actual): ")
            
            if pos_input.strip():
                try:
                    x, y = map(float, pos_input.split())
                    # Validar que las coordenadas estén dentro del espacio
                    x = max(0, min(x, self.field_width))
                    y = max(0, min(y, self.field_length))
                    self.anchors[anchor_id]['position'] = [x, y, self.anchor_height] # Update with new common height
                    print(f"Posición actualizada: [{x:.2f}, {y:.2f}, {self.anchor_height:.2f}]")
                except ValueError:
                    print("Formato inválido. Manteniendo la posición actual.")
            else:
                 # Ensure height is updated even if X,Y are kept
                 self.anchors[anchor_id]['position'][2] = self.anchor_height 

        self.save_anchor_positions()
        print("\nConfiguración de Anchors finalizada.")

    def load_data(self, csv_file=None):
        """Carga los datos desde un archivo CSV PROCESADO."""
        if csv_file is None:
            # Usa select_csv_file si no se proporciona un archivo
            csv_file = self.select_csv_file()
            if csv_file is None:
                 print("No se seleccionó archivo CSV. Saliendo.")
                 return False
            
        print(f"Cargando datos procesados desde: {csv_file}")
            
        try:
            # Leer el CSV procesado
            df = pd.read_csv(csv_file, header=0, na_values=['NaN', '', ' ']) 
            print(f"Columnas leídas del CSV procesado: {df.columns.tolist()}")
            
            # Verificar columnas esenciales del archivo procesado
            essential_cols = ['Timestamp(ms)', 'TagID', 'Position_X', 'Position_Y', 'Position_Z']
            missing_essentials = [col for col in essential_cols if col not in df.columns]
            if missing_essentials:
                print(f"Error: Faltan columnas esenciales en el archivo CSV PROCESADO: {missing_essentials}")
                return False

            # Convertir columnas relevantes a numérico (aunque deberían estarlo)
            numeric_cols = ['Timestamp(ms)', 'TagID', 'Position_X', 'Position_Y', 'Position_Z']
            # Añadir columnas de distancia y RSSI si existen para la visualización
            for anchor_id in self.anchors.keys():
                numeric_cols.append(f'FilteredDistance_{anchor_id}')
                numeric_cols.append(f'RSSI_{anchor_id}')

            for col in numeric_cols:
                if col in df.columns:
                    df[col] = pd.to_numeric(df[col], errors='coerce')
            
            # Eliminar filas donde las columnas ESENCIALES REALMENTE CRÍTICAS son NaN
            # Permitiremos NaN en Position_X/Y/Z, el código de visualización los manejará.
            critical_for_replay = ['Timestamp(ms)', 'TagID']
            initial_rows = len(df)
            df.dropna(subset=critical_for_replay, inplace=True)
            if len(df) < initial_rows:
                 print(f"Eliminadas {initial_rows - len(df)} filas con NaN en Timestamp o TagID.")

            if df.empty:
                print("No se encontraron datos válidos después de la limpieza de Timestamp/TagID.")
                return False
            
            # Convertir tipos para IDs (después de dropna)
            df['TagID'] = df['TagID'].astype(int)
            df['Timestamp(ms)'] = df['Timestamp(ms)'].astype(int)

            # Ordenar por timestamp (aunque ya debería estarlo)
            df.sort_values(by='Timestamp(ms)', inplace=True)
            df.reset_index(drop=True, inplace=True)

            # Agrupar por Tag ID y convertir a formato de lista de diccionarios
            self.all_data = {}
            for tag_id, group in df.groupby('TagID'):
                tag_data_list = []
                for _, row in group.iterrows():
                    distances = {aid: row.get(f'FilteredDistance_{aid}', np.nan) / 100.0 
                                 for aid in self.anchors.keys()} # Convertir a metros
                    rssis = {aid: row.get(f'RSSI_{aid}', np.nan) 
                             for aid in self.anchors.keys()}
                    # La posición ya está calculada
                    position_3d = [row['Position_X'], row['Position_Y'], row['Position_Z']]
                    
                    tag_data_list.append({
                        'timestamp': row['Timestamp(ms)'],
                        'position': position_3d, # Almacenar [X, Y, Z]
                        'distances': distances, # Distancias en metros
                        'rssis': rssis,
                        # Podríamos añadir AnchorStatus si esa columna se mantiene en el procesado
                        # 'statuses': {aid: row.get(f'AnchorStatus_{aid}', 0) for aid in self.anchors.keys()} 
                    })
                self.all_data[tag_id] = tag_data_list
            
            self.tag_ids_available = sorted(list(self.all_data.keys()))

            if not self.tag_ids_available:
                print("No se encontraron datos válidos de tags en el archivo procesado.")
                return False

            self.selected_tag_id = self.tag_ids_available[0]
            self.total_frames = len(self.all_data[self.selected_tag_id])
            self.current_frame = 0 
            
            # Inicializar rastros para cada tag
            for tag_id in self.tag_ids_available:
                self.tag_trails[tag_id] = deque(maxlen=self.trail_length)

            print(f"Datos procesados cargados. Tags: {self.tag_ids_available}. Mostrando Tag: {self.selected_tag_id}. Frames: {self.total_frames}")
            return True

        except Exception as e:
            import traceback
            print(f"Error inesperado al cargar datos procesados: {e}")
            traceback.print_exc()
            return False

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
        for anchor_id, props in self.anchors.items():
            pos = props['position']
            # Store plot object to update alpha later
            self.anchor_plots[anchor_id] = self.ax.plot(pos[0], pos[1], 'o', markersize=10, color=props['color'], label=props['label'])[0]
            self.ax.text(pos[0] + 0.1, pos[1], str(anchor_id))
            # Inicializar círculos de radio (ocultos)
            self.radius_circles[anchor_id] = plt.Circle((pos[0], pos[1]), 0.1, color=props['color'], fill=False, linestyle='--', visible=False)
            self.ax.add_patch(self.radius_circles[anchor_id])
        
        # --- Plot inicial para el tag y su rastro --- 
        if self.selected_tag_id and self.selected_tag_id in self.all_data:
            initial_pos = [np.nan, np.nan]
            if len(self.all_data[self.selected_tag_id]) > 0:
                first_valid_pos = next((p['position'] for p in self.all_data[self.selected_tag_id] if not np.isnan(p['position'][0])), None)
                if first_valid_pos: initial_pos = first_valid_pos[:2] # Usar X,Y
            
            # Plot del tag
            self.tag_plots[self.selected_tag_id] = self.ax.plot(initial_pos[0], initial_pos[1], 'X', markersize=12, color='black', label=f'Tag {self.selected_tag_id}')[0]
            
            # Plot del rastro (inicialmente vacío)
            self.trail_plots[self.selected_tag_id] = self.ax.plot([], [], '-', color='cyan', linewidth=1.5, alpha=0.6)[0]
        # ------------------------------------------

        # Añadir texto para información
        self.info_text = self.ax.text(0.02, 0.95, '', transform=self.ax.transAxes, verticalalignment='top', 
                                      bbox=dict(boxstyle='round,pad=0.5', fc='wheat', alpha=0.5))
        self.time_text = self.ax.text(0.98, 0.95, '', transform=self.ax.transAxes, verticalalignment='top', horizontalalignment='right',
                                     bbox=dict(boxstyle='round,pad=0.5', fc='lightblue', alpha=0.5))

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
        
        # Iniciar la animación con intervalo más corto para fluidez
        self.animation = FuncAnimation(
            self.fig, 
            self.update, 
            frames=range(self.total_frames),
            interval=50,  # Ajustar intervalo si es necesario 
            blit=False,  # Desactivar blit para forzar redibujado completo
            repeat=True
        )
    
    def update(self, frame):
        """Actualiza la animación para el cuadro actual."""
        if not self.playing or frame >= self.total_frames:
            self.playing = False # Stop if paused or reached end
            return [] # No updates
            
        self.current_frame = frame
        
        if self.selected_tag_id not in self.all_data or frame >= len(self.all_data[self.selected_tag_id]):
             print(f"Frame {frame} fuera de rango para tag {self.selected_tag_id}")
             return []
             
        current_data = self.all_data[self.selected_tag_id][frame]
        position_3d = current_data['position'] # Leer [X, Y, Z] directamente
        distances = current_data['distances']
        # statuses = current_data['statuses'] # Si añadimos status al procesado
        timestamp_ms = current_data['timestamp']

        position_xy = [np.nan, np.nan] # Posición 2D para plotear
        position_z = np.nan # Coordenada Z

        # Actualizar posición del tag si es válida
        if position_3d is not None and not np.isnan(position_3d[0]):
            position_xy = position_3d[:2]
            position_z = position_3d[2]
            self.tag_plots[self.selected_tag_id].set_data([position_xy[0]], [position_xy[1]]) 
            self.tag_plots[self.selected_tag_id].set_visible(True)
            self.last_valid_position = position_xy # Guardar X,Y para fallback
            # Añadir a la cola del rastro
            self.tag_trails[self.selected_tag_id].append(position_xy)
        else:
            if hasattr(self, 'last_valid_position') and self.last_valid_position is not None:
                 self.tag_plots[self.selected_tag_id].set_data([self.last_valid_position[0]], [self.last_valid_position[1]])
                 self.tag_plots[self.selected_tag_id].set_visible(True)
                 # No añadir NaN al rastro
            else:
                self.tag_plots[self.selected_tag_id].set_visible(False)
                # Vaciar rastro si la primera posición es inválida
                self.tag_trails[self.selected_tag_id].clear()
        
        # Actualizar el plot del rastro
        if self.selected_tag_id in self.trail_plots:
            trail_data = np.array(list(self.tag_trails[self.selected_tag_id]))
            if trail_data.size > 0:
                 self.trail_plots[self.selected_tag_id].set_data(trail_data[:, 0], trail_data[:, 1])
            else:
                 self.trail_plots[self.selected_tag_id].set_data([], [])

        # --- Actualizar Texto Informativo (incluyendo Z) --- 
        info_str = (f'Tag: {self.selected_tag_id}\nFrame: {frame}/{self.total_frames-1}\n'
                   f'Pos (X,Y,Z): ({position_xy[0]:.2f}, {position_xy[1]:.2f}, {position_z:.2f}) m\n' # Mostrar Z
                   f'Distances (m):\n')

        # Actualizar círculos de radio y estado de anchors
        for anchor_id, props in self.anchors.items():
            dist = distances.get(anchor_id, np.nan)
            # Necesitamos leer el status si está disponible
            # status = statuses.get(anchor_id, 0) 
            status = 1 if pd.notna(dist) and dist > 0 else 0 # Asumir OK si hay distancia válida
            
            if status == 1:
                self.anchor_plots[anchor_id].set_alpha(1.0) 
            else:
                self.anchor_plots[anchor_id].set_alpha(0.3)

            info_str += f"  A{anchor_id}: {dist:.2f} ({'OK' if status==1 else 'FAIL'})"
            
            circle = self.radius_circles[anchor_id]
            if not np.isnan(dist) and dist > 0 and status == 1:
                circle.set_radius(dist)
                circle.center = self.anchor_plots[anchor_id].get_data() # Center on anchor
                circle.set_visible(True)
            else:
                circle.set_visible(False)

        self.info_text.set_text(info_str)
        
        # Actualizar tiempo
        elapsed_time_s = (timestamp_ms - self.all_data[self.selected_tag_id][0]['timestamp']) / 1000.0
        self.time_text.set_text(f'Tiempo: {elapsed_time_s:.2f} s')

        # Devolver los elementos modificados para blitting
        updated_elements = [self.tag_plots[self.selected_tag_id], 
                           self.trail_plots[self.selected_tag_id], # Añadir rastro a elementos actualizados
                           self.info_text, self.time_text] + \
                           list(self.radius_circles.values()) + list(self.anchor_plots.values())
        return updated_elements

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

    def run(self):
        """Ejecuta el visor completo con funcionalidad interactiva."""
        if self.load_data(): # load_data ahora manejará la selección del archivo PROCESADO
            # Crear la visualización y la animación
            self.create_visualization()
            
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

    def select_csv_file(self):
        """Abre un diálogo para seleccionar un archivo CSV PROCESADO."""
        root = Tk()
        root.withdraw()  # Ocultar la ventana principal
        
        # Directorio inicial para el diálogo
        initial_dir = os.path.join(os.getcwd(), "data_recordings")
        if not os.path.exists(initial_dir):
            initial_dir = os.getcwd()
        
        # Abrir diálogo para seleccionar archivo
        file_path = filedialog.askopenfilename(
            title="Seleccione un archivo CSV PROCESADO (con posiciones)",
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

    def generate_heatmap(self):
        """Genera un mapa de calor de las posiciones del tag."""
        if not self.all_data or self.selected_tag_id not in self.all_data:
            print("No hay datos cargados...")
            return
        
        # Extraer posiciones X,Y válidas directamente
        positions_array = np.array([p['position'][:2] 
                                 for p in self.all_data[self.selected_tag_id] 
                                 if p['position'] is not None and not np.isnan(p['position'][0])])
        
        if positions_array.shape[0] == 0:
             print("No hay posiciones válidas...")
             return

        min_x, min_y = np.min(positions_array, axis=0)
        max_x, max_y = np.max(positions_array, axis=0)

        # Añadir un margen para asegurar que todos los puntos estén dentro
        margin = 1.0 # Ajusta este margen según sea necesario
        self.field_width = (max_x - min_x) + 2 * margin
        self.field_height = (max_y - min_y) + 2 * margin
        self.origin_offset = np.array([min_x - margin, min_y - margin])

        if self.field_width <= 0 or self.field_height <= 0:
             print(f"Dimensiones del campo inválidas: width={self.field_width}, height={self.field_height}. Ajustando a valores mínimos.")
             # Asignar un tamaño mínimo si las dimensiones son inválidas o cero
             self.field_width = max(self.field_width, margin * 2)
             self.field_height = max(self.field_height, margin * 2)
             # Recalcular el offset si es necesario basado en un punto de referencia o dejarlo como está

        grid_size = 100  # Tamaño de la cuadrícula para el mapa de calor
        heatmap = np.zeros((grid_size, grid_size))

        for pos in positions_array: # Usar solo posiciones válidas
            # Normalizar posición relativa al origen del heatmap (esquina inferior izquierda)
            relative_pos = pos - self.origin_offset

            # Asegurarse de que relative_pos no contenga NaN (aunque ya filtramos, doble chequeo)
            if np.isnan(relative_pos[0]) or np.isnan(relative_pos[1]):
                continue # Saltar este punto si es NaN

            # Calcular índices en la cuadrícula, asegurándose de que estén dentro de los límites
            # Evitar división por cero si field_width o field_height son <= 0
            if self.field_width > 0 and self.field_height > 0:
                 x_idx = int(relative_pos[0] / self.field_width * (grid_size - 1))
                 y_idx = int(relative_pos[1] / self.field_height * (grid_size - 1))

                 # Asegurarse de que los índices estén dentro del rango [0, grid_size-1]
                 x_idx = np.clip(x_idx, 0, grid_size - 1)
                 y_idx = np.clip(y_idx, 0, grid_size - 1)

                 heatmap[y_idx, x_idx] += 1 # Incrementar la celda correspondiente
            else:
                 # Opcional: Loguear si se salta un punto debido a dimensiones inválidas
                 # print(f"Skipping point {pos} due to invalid field dimensions.")
                 pass

        if np.sum(heatmap) == 0:
            print("No hay datos válidos para el mapa de calor.")
            return

        # Crear figura
        plt.figure(figsize=(10, 8))
        
        # Dibujar mapa de calor
        plt.imshow(heatmap, extent=[0, self.field_width, 0, self.field_height], 
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

if __name__ == "__main__":
    replay = TagReplay()
    # replay.setup_anchors() # Descomentar si quieres configurar anchors al inicio
    replay.run()
