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
    def __init__(self, tag_id_to_show=None):
        # Configuración del espacio experimental
        self.field_length = 5.1   # metros (largo, eje Y)
        self.field_width = 3.45   # metros (ancho, eje X)
        self.anchor_height = 1.5 
        self.time_window_ms = 100 # Ventana de tiempo en ms para agrupar lecturas
        
        # Posiciones de los anchors (x, y, z) en metros
        self.anchors = {
            10: {'position': np.array([0.0, 1.10, self.anchor_height]), 'color': 'red', 'label': 'Anchor 10'},
            20: {'position': np.array([0.0, 4.55, self.anchor_height]), 'color': 'green', 'label': 'Anchor 20'},
            30: {'position': np.array([3.45, 4.55, self.anchor_height]), 'color': 'blue', 'label': 'Anchor 30'},
            40: {'position': np.array([3.45, 1.10, self.anchor_height]), 'color': 'purple', 'label': 'Anchor 40'},
        }
        self.anchor_ids = list(self.anchors.keys())
        self.anchor_coords_array = np.array([self.anchors[aid]['position'] for aid in self.anchor_ids])

        self.tag_id_to_show = tag_id_to_show
        self.filepath = None
        self.df_all = None # DataFrame con todos los datos crudos
        self.tag_data_raw = None # DataFrame con datos crudos del tag seleccionado
        self.timestamps = [] # Lista de timestamps únicos para los frames de la animación
        # self.positions = [] # Ya no precalculamos
        # self.position_qualities = [] 
        self.total_frames = 0
        self.current_frame = 0
        self.playing = False
        self.animation = None
        self.fig = None
        self.ax = None
        self.lines = []
        self.points = []
        self.time_text = None
        self.frame_slider = None
        self.play_button = None
        self.reset_button = None
        self.tag_select_menu = None
        self.legend = None
        self.last_valid_position = None # Para mostrar si falla el cálculo actual

    def _error_function(self, tag_pos_xy, anchor_positions, measured_distances):
        """Función de error para la optimización (diferencia cuadrática de distancias)."""
        tag_pos_3d = np.array([tag_pos_xy[0], tag_pos_xy[1], 0.0]) # Asumir Z=0
        error = 0.0
        valid_measurements = 0
        for i in range(len(anchor_positions)):
            if measured_distances[i] is not None and not np.isnan(measured_distances[i]): # Check for None too
                calculated_distance = np.linalg.norm(tag_pos_3d - anchor_positions[i])
                error += (calculated_distance - measured_distances[i])**2
                valid_measurements += 1
        return error / valid_measurements if valid_measurements > 0 else np.inf

    def calculate_position(self, measured_distances):
        """Calcula la posición 2D usando optimización (minimize)."""
        valid_indices = [i for i, d in enumerate(measured_distances) if d is not None and not np.isnan(d) and d > 0.01] # Añadir d > 0.01
        
        if len(valid_indices) < 3:
            return None, None, np.inf 
            
        anchors_subset = self.anchor_coords_array[valid_indices]
        distances_subset = np.array([measured_distances[i] for i in valid_indices]) # Use list comprehension
        
        # Usar última posición válida como estimación inicial si existe
        if self.last_valid_position is not None:
             initial_guess = self.last_valid_position[:2]
        else: # Si no, usar centroide
             initial_guess = np.mean(anchors_subset, axis=0)[:2] 
        
        bounds = [(0, self.field_width), (0, self.field_length)]

        result = minimize(
            self._error_function, 
            initial_guess, 
            args=(anchors_subset, distances_subset),
            method='L-BFGS-B', 
            bounds=bounds,
            options={'maxiter': 50, 'ftol': 1e-7} # Ajustar opciones de optimización
        )

        if result.success and result.fun < 0.5: # Añadir umbral de calidad (ajustar 0.5)
            pos_xy = result.x[:2]
            self.last_valid_position = np.array([pos_xy[0], pos_xy[1], 0.0]) # Guardar como última válida
            return pos_xy[0], pos_xy[1], result.fun 
        else:
            # print(f"Optimización fallida o calidad pobre: {result.message}, fun: {result.fun}")
            return None, None, result.fun # Devolver calidad aunque falle

    def load_data(self):
        """Carga los datos desde un archivo CSV **CRUDO** seleccionado."""
        options = {
            'initialdir': os.path.join(os.getcwd(), 'uwb_logs_mqtt'),
            'title': 'Selecciona archivo CSV **CRUDO** (log_*.csv)',
            # Ajustar filtro para logs crudos
            'filetypes': (('Raw Log CSV files', 'log_*.csv'), ('CSV files', '*.csv'), ('all files', '*.*'))
        }
        filepath = filedialog.askopenfilename(**options)
        if not filepath:
            print("No se seleccionó ningún archivo.")
            return False
        
        print(f"Archivo seleccionado: {filepath}")
        self.filepath = filepath

        try:
            # Leer CSV crudo
            self.df_all = pd.read_csv(filepath)
            print(f"Leídas {len(self.df_all)} filas.")
            
            # Verificar columnas esperadas del formato CRUDO
            # Asegúrate que estos nombres coinciden EXACTAMENTE con tu CSV crudo
            expected_cols = ['Timestamp(ms)', 'TagID', 'AnchorID', 'FilteredDistance(cm)', 'RSSI(dBm)']
            if not all(col in self.df_all.columns for col in expected_cols):
                print("Error: El archivo CSV no parece tener el formato crudo esperado.")
                print(f"Columnas esperadas: {expected_cols}")
                print(f"Columnas encontradas: {self.df_all.columns.tolist()}")
                return False

            # Limpieza inicial (convertir tipos, quitar NaNs en columnas clave)
            for col in ['Timestamp(ms)', 'TagID', 'AnchorID', 'FilteredDistance(cm)', 'RSSI(dBm)']:
                 self.df_all[col] = pd.to_numeric(self.df_all[col], errors='coerce')
            self.df_all.dropna(subset=['Timestamp(ms)', 'TagID', 'AnchorID', 'FilteredDistance(cm)'], inplace=True)
            self.df_all['TagID'] = self.df_all['TagID'].astype(int)
            self.df_all['AnchorID'] = self.df_all['AnchorID'].astype(int)
            self.df_all.sort_values(by='Timestamp(ms)', inplace=True)
            
            self.unique_tags = sorted(self.df_all['TagID'].unique())

            if not self.unique_tags:
                print("Error: No se encontraron IDs de Tag válidos en el archivo.")
                return False
            
            if self.tag_id_to_show is None or self.tag_id_to_show not in self.unique_tags:
                self.tag_id_to_show = self.unique_tags[0]
                print(f"Mostrando el primer tag disponible: {self.tag_id_to_show}")
            else:
                 print(f"Mostrando Tag ID: {self.tag_id_to_show}")

            # Filtrar datos crudos para el tag seleccionado
            self.tag_data_raw = self.df_all[self.df_all['TagID'] == self.tag_id_to_show].copy()
            # Crear lista de timestamps únicos para los frames de la animación
            self.timestamps = sorted(self.tag_data_raw['Timestamp(ms)'].unique())
            self.total_frames = len(self.timestamps)

            if self.total_frames == 0:
                print(f"Advertencia: No hay datos para el Tag ID {self.tag_id_to_show}.")
                return False

            # --- NO SE PRECALCULAN POSICIONES --- 
            print(f"Datos crudos cargados para Tag ID {self.tag_id_to_show}. Total frames (timestamps únicos): {self.total_frames}")
            self.last_valid_position = None # Resetear última posición válida
            return True

        except FileNotFoundError:
            print(f"Error: Archivo no encontrado en {filepath}")
            return False
        except Exception as e:
            print(f"Error inesperado al cargar o procesar los datos crudos: {e}")
            import traceback
            traceback.print_exc()
            return False

    def create_visualization(self):
        """Crea la figura y los elementos de la animación."""
        # ... (Sin cambios respecto a la versión anterior) ...
        self.fig, self.ax = plt.subplots(figsize=(self.field_width * 1.5, self.field_length * 1.5))
        self.ax.set_xlim(0, self.field_width)
        self.ax.set_ylim(0, self.field_length)
        self.ax.set_xlabel("Ancho (X) [m]")
        self.ax.set_ylabel("Largo (Y) [m]")
        self.ax.set_title(f"Replay UWB - Tag {self.tag_id_to_show} - Archivo: {os.path.basename(self.filepath)}")
        self.ax.set_aspect('equal', adjustable='box')
        self.ax.grid(True)

        # Dibujar Anclas
        anchor_points = []
        for anchor_id, props in self.anchors.items():
            pos = props['position']
            color = props['color']
            label = props['label']
            point = self.ax.scatter(pos[0], pos[1], color=color, s=100, label=label, marker='s') # s=size, marker='s' (square)
            anchor_points.append(point)
            self.ax.text(pos[0] + 0.1, pos[1], str(anchor_id), color=color, fontsize=9)

        # Elementos dinámicos
        self.lines = [self.ax.plot([], [], linestyle='--', color=self.anchors[aid]['color'], alpha=0.7)[0] for aid in self.anchor_ids]
        tag_point, = self.ax.plot([], [], 'bo', markersize=8, label='Tag') 
        self.points = [tag_point]
        self.time_text = self.ax.text(0.02, 0.95, '', transform=self.ax.transAxes)
        handles, labels = self.ax.get_legend_handles_labels()
        self.legend = self.ax.legend(handles=handles, labels=labels, loc='upper right')

        # Controles
        plt.subplots_adjust(bottom=0.25)
        slider_ax = plt.axes([0.2, 0.1, 0.65, 0.03])
        self.frame_slider = plt.Slider(slider_ax, 'Frame', 0, max(0, self.total_frames - 1), valinit=0, valstep=1)
        self.frame_slider.on_changed(self.set_frame)
        play_ax = plt.axes([0.8, 0.025, 0.1, 0.04])
        self.play_button = plt.Button(play_ax, 'Play/Pause')
        self.play_button.on_clicked(self.toggle_play)
        reset_ax = plt.axes([0.68, 0.025, 0.1, 0.04])
        self.reset_button = plt.Button(reset_ax, 'Reset')
        self.reset_button.on_clicked(self.reset_animation)

        # Iniciar animación
        self.animation = FuncAnimation(
            self.fig, 
            self.update, 
            frames=range(self.total_frames),
            interval=max(20, self.time_window_ms // 2), # Intervalo basado en window, mínimo 20ms
            blit=False, 
            repeat=True
        )
        
        print("Visualización creada. Iniciando reproducción...")
        self.playing = True
        plt.show()

    def set_frame(self, val):
        """Actualiza el frame actual basado en el slider."""
        new_frame = int(val)
        if 0 <= new_frame < self.total_frames:
             self.current_frame = new_frame
             if not self.playing:
                 self.update(self.current_frame)
                 self.fig.canvas.draw_idle()
        else:
             print(f"Advertencia: Frame {new_frame} fuera de rango [0, {self.total_frames-1}]")

    def toggle_play(self, event):
        # ... (Sin cambios) ...
        self.playing = not self.playing
        if self.playing:
            self.animation.resume() # Reanuda la animación de matplotlib
            print("Reproduciendo")
        else:
            self.animation.pause() # Pausa la animación de matplotlib
            print("Pausado")

    def reset_animation(self, event):
        # ... (Sin cambios) ...
        self.playing = False
        self.animation.pause()
        self.current_frame = 0
        self.last_valid_position = None # Resetear última posición
        self.frame_slider.set_val(0)
        self.update(0) 
        self.fig.canvas.draw_idle()
        print("Animación reseteada")

    def update(self, frame):
        """Actualiza la animación para el cuadro actual, calculando posición sobre la marcha."""
        if not self.tag_data_raw or frame >= self.total_frames or frame < 0:
            return self.lines + self.points + [self.time_text]
        
        # Actualizar slider si está reproduciendo
        if self.playing:
             self.current_frame = frame
             # Evitar error si el slider no está listo o el frame es inválido
             try:
                 if self.frame_slider and 0 <= frame < self.total_frames:
                     self.frame_slider.set_val(frame)
             except Exception as e:
                 print(f"Error actualizando slider: {e}") # Debug raro
        
        target_frame_index = frame 
        current_time_ms = self.timestamps[target_frame_index]
        self.time_text.set_text(f'Time: {current_time_ms / 1000.0:.2f} s')

        # --- Lógica de Ventana de Tiempo --- 
        window_start_time_ms = current_time_ms - self.time_window_ms
        # Seleccionar datos crudos dentro de la ventana
        window_data = self.tag_data_raw[
            (self.tag_data_raw['Timestamp(ms)'] > window_start_time_ms) & 
            (self.tag_data_raw['Timestamp(ms)'] <= current_time_ms)
        ]
        
        latest_distances_cm = {} # Guardar la última distancia de cada ancla en la ventana
        if not window_data.empty:
             # Agrupar por AnchorID y obtener el índice del último timestamp para cada uno
            latest_indices = window_data.groupby('AnchorID')['Timestamp(ms)'].idxmax()
            latest_readings = window_data.loc[latest_indices]
            # Crear diccionario AnchorID -> Distancia(cm)
            latest_distances_cm = latest_readings.set_index('AnchorID')['FilteredDistance(cm)'].to_dict()
            
        # Preparar lista de distancias en METROS para calculate_position
        measured_distances_m = []
        for anchor_id in self.anchor_ids:
             dist_cm = latest_distances_cm.get(anchor_id, np.nan) # Obtener de dict, NaN si no está
             measured_distances_m.append(dist_cm / 100.0 if not pd.isna(dist_cm) else np.nan)
        # --- Fin Lógica Ventana --- 

        # Calcular posición para este frame/ventana
        pos_x, pos_y, quality = self.calculate_position(measured_distances_m)

        # Actualizar punto del tag
        if pos_x is not None and pos_y is not None:
            self.points[0].set_data([pos_x], [pos_y])
            self.points[0].set_visible(True)
            # Opcional: Colorear por calidad
            # quality_color = plt.cm.viridis(min(quality / 0.1, 1.0))
            # self.points[0].set_color(quality_color)
        elif self.last_valid_position is not None: # Si falla pero había una antes
             self.points[0].set_data([self.last_valid_position[0]], [self.last_valid_position[1]])
             self.points[0].set_visible(True) # Mantener visible en la última buena
        else: # Si falla y no hay ninguna anterior
            self.points[0].set_visible(False) # Ocultar punto

        # Actualizar líneas y etiquetas de distancia (desde los datos de la ventana)
        for i, anchor_id in enumerate(self.anchor_ids):
            anchor_pos = self.anchors[anchor_id]['position']
            current_pos_x = pos_x if pos_x is not None else (self.last_valid_position[0] if self.last_valid_position is not None else np.nan)
            current_pos_y = pos_y if pos_y is not None else (self.last_valid_position[1] if self.last_valid_position is not None else np.nan)
            
            # Solo dibujar línea si la posición actual es válida
            if not np.isnan(current_pos_x):
                 self.lines[i].set_data([anchor_pos[0], current_pos_x], [anchor_pos[1], current_pos_y])
                 self.lines[i].set_visible(True)
                 # Mostrar distancia de esta ventana si existe
                 dist_val_m = measured_distances_m[i]
                 if not np.isnan(dist_val_m):
                     self.lines[i].set_label(f'{dist_val_m:.2f}m')
                 else:
                     self.lines[i].set_label('N/A')
            else:
                 self.lines[i].set_visible(False)
                 self.lines[i].set_label('N/A')

        # Devolver elementos gráficos que han cambiado
        return self.lines + self.points + [self.time_text]

    def run(self):
        """Inicia el proceso: carga datos crudos y crea visualización."""
        if self.load_data():
            self.create_visualization()
        else:
            print("No se pudieron cargar los datos crudos. Saliendo.")

# --- Punto de entrada --- 
if __name__ == "__main__":
    tag_id = None
    root = Tk()
    root.withdraw()
    replay = TagReplay(tag_id_to_show=tag_id)
    replay.run()
