import pandas as pd
import numpy as np
import argparse
import json
import os
from scipy.optimize import minimize

# --- Constantes y Configuración ---
# Columnas esperadas en el archivo de log crudo
RAW_COLUMN_NAMES = [
    'TagID', 'Timestamp(ms)', 'AnchorID', 
    'RawDistance(cm)', 'FilteredDistance(cm)', 
    'RSSI(dBm)', 'AnchorStatus'
]
# Columnas a usar como valores al pivotar
PIVOT_VALUE_COLS = ['FilteredDistance(cm)', 'RSSI(dBm)']
# Columnas de índice para pivotar
PIVOT_INDEX_COLS = ['Timestamp(ms)', 'TagID']
# Columna que contiene el ID del ancla
PIVOT_COLUMN_COL = 'AnchorID'
# Archivo de configuración para posiciones de anclas
ANCHOR_CONFIG_FILE = 'anchor_positions.json'
# Altura por defecto si no está en el config
DEFAULT_ANCHOR_HEIGHT = 1.5 

# --- Funciones ---

def load_anchor_positions(config_file, anchors_dict):
    """Carga y actualiza las posiciones de los anchors desde un archivo JSON."""
    if os.path.exists(config_file):
        try:
            with open(config_file, 'r') as f:
                config = json.load(f)
                for anchor_id_str, data in config.items():
                    try:
                        anchor_id = int(anchor_id_str)
                        pos = data.get('position')
                        # Asegurar que la posición es una lista de 3 números (X, Y, Z)
                        if isinstance(pos, list) and len(pos) == 3 and all(isinstance(n, (int, float)) for n in pos):
                             if anchor_id not in anchors_dict:
                                 anchors_dict[anchor_id] = {} # Crear entrada si no existe
                             anchors_dict[anchor_id]['position'] = pos[:3]
                        elif isinstance(pos, list) and len(pos) == 2 and all(isinstance(n, (int, float)) for n in pos):
                             print(f"Advertencia: Anchor {anchor_id} en config solo tiene X,Y. Añadiendo Z={DEFAULT_ANCHOR_HEIGHT}.")
                             if anchor_id not in anchors_dict:
                                 anchors_dict[anchor_id] = {}
                             anchors_dict[anchor_id]['position'] = pos + [DEFAULT_ANCHOR_HEIGHT]
                        else:
                             print(f"Advertencia: Posición inválida para anchor {anchor_id} en {config_file}. Ignorando.")
                    except ValueError:
                         print(f"Advertencia: ID de anchor inválido '{anchor_id_str}' en {config_file}. Ignorando.")
            print(f"Posiciones de anchors cargadas/actualizadas desde {config_file}")
        except Exception as e:
            print(f"Error al cargar/parsear {config_file}: {e}. Usando/manteniendo predeterminadas si existen.")
    else:
        print(f"Archivo de configuración '{config_file}' no encontrado. Usando posiciones predeterminadas si existen.")
    
    # Asegurar que todas las anclas predefinidas tengan una posición Z
    # Esto es por si el diccionario inicial no tenía Z o el config falló
    for aid, data in anchors_dict.items():
         if 'position' not in data or len(data['position']) < 3:
             print(f"Advertencia: Anchor {aid} no tenía posición Z completa. Estableciendo Z a {DEFAULT_ANCHOR_HEIGHT}.")
             x = data.get('position', [0,0])[0]
             y = data.get('position', [0,0])[1]
             anchors_dict[aid]['position'] = [x, y, DEFAULT_ANCHOR_HEIGHT]
         elif len(data['position']) > 3:
             anchors_dict[aid]['position'] = data['position'][:3] # Truncar si hay más de 3


# Usar la misma función de multilateración que el replay para consistencia
def multilateration_3d(responding_distances, responding_anchor_positions):
    """Calcula la posición 3D del tag usando multilateración optimizada."""
    anchor_ids = list(responding_distances.keys())
    if len(anchor_ids) < 3: # Necesitamos al menos 3 para 3D (aunque 4 es mejor)
        return None 

    def error_function(point): # point is [x, y, z]
        error = 0
        for anchor_id in anchor_ids:
            anchor_pos = responding_anchor_positions[anchor_id]
            measured_dist = responding_distances[anchor_id]
            if measured_dist <= 0: # Ignorar distancias no válidas
                continue 
            calculated_dist_sq = (
                (point[0] - anchor_pos[0])**2 + 
                (point[1] - anchor_pos[1])**2 + 
                (point[2] - anchor_pos[2])**2
            )
            # Evitar sqrt de números negativos pequeños debido a precisión flotante
            if calculated_dist_sq < 0: calculated_dist_sq = 0 
            calculated_dist = np.sqrt(calculated_dist_sq)
            error += (calculated_dist - measured_dist)**2
        return error

    # Usar una estimación inicial basada en las posiciones de las anclas que responden
    initial_guess = np.mean([responding_anchor_positions[aid] for aid in anchor_ids], axis=0).tolist()
    
    # Límites razonables (ej. -10m a +10m de las anclas, ajustar si es necesario)
    all_anchor_pos = np.array([pos for pos in responding_anchor_positions.values()])
    min_coords = np.min(all_anchor_pos, axis=0) - 10
    max_coords = np.max(all_anchor_pos, axis=0) + 10
    bounds = list(zip(min_coords, max_coords))

    result = minimize(error_function, initial_guess, method='L-BFGS-B', bounds=bounds)

    if result.success and result.fun < 1.0: # Añadir un umbral de error (ej. 1.0 m^2 total)
        return result.x # Devuelve [x, y, z]
    else:
        # print(f"Optimización fallida o error alto: {result.message} (Error: {result.fun:.2f})")
        return None


def process_uwb_log(input_file, output_file):
    """Carga, pivota y aplica post-procesamiento (cálculo de posición) a un log UWB."""
    print(f"Procesando archivo: {input_file}")

    # Definir estructura inicial de anchors (será actualizada desde JSON si existe)
    # Es importante tener al menos los IDs que esperamos ver en los datos
    # Las posiciones se sobreescribirán/completarán desde el archivo
    anchors_config = {
        10: {}, 20: {}, 30: {}, 40: {} # IDs esperados
    }
    load_anchor_positions(ANCHOR_CONFIG_FILE, anchors_config)
    print("Configuración de Anchors a usar:", anchors_config)
    
    # Extraer solo las posiciones para la multilateración
    anchor_positions_map = {aid: data['position'] for aid, data in anchors_config.items() if 'position' in data}
    if len(anchor_positions_map) < 3:
        print("Error: No se pudieron cargar suficientes posiciones de anclas (>=3) para calcular la posición.")
        return

    try:
        # Intentar detectar separador automáticamente, o especificar si es necesario
        # Especificar dtype puede ayudar, pero lo haremos explícito después
        df = pd.read_csv(input_file, header=None, names=RAW_COLUMN_NAMES, on_bad_lines='warn')
        print(f"Archivo leído con éxito. Columnas detectadas: {df.columns.tolist()}")
        if df.shape[1] != len(RAW_COLUMN_NAMES):
             print(f"Advertencia: El número de columnas esperado ({len(RAW_COLUMN_NAMES)}) no coincide con las columnas leídas ({df.shape[1]}). Verifica el separador o el formato del CSV.")
             # Podrías intentar leer de nuevo con otro separador si el primero falla
             # df = pd.read_csv(input_file, header=None, names=RAW_COLUMN_NAMES, sep=';') # Ejemplo con punto y coma

        # --- NUEVO: Forzar conversión a numérico --- 
        numeric_cols = ['FilteredDistance(cm)', 'RSSI(dBm)', 'RawDistance(cm)', 'Timestamp(ms)', 'AnchorID', 'TagID', 'AnchorStatus']
        for col in numeric_cols:
            if col in df.columns:
                df[col] = pd.to_numeric(df[col], errors='coerce')
            else:
                print(f"Advertencia: La columna numérica esperada '{col}' no se encontró en el CSV.")
                
        # Eliminar filas donde la conversión falló (resultó en NaN) en columnas críticas
        critical_cols = ['Timestamp(ms)', 'TagID', 'AnchorID', 'FilteredDistance(cm)', 'RSSI(dBm)']
        initial_rows = len(df)
        df.dropna(subset=critical_cols, inplace=True)
        if len(df) < initial_rows:
            print(f"Eliminadas {initial_rows - len(df)} filas con valores no numéricos o NaN en columnas críticas.")

    except pd.errors.EmptyDataError:
        print(f"Error: El archivo {input_file} está vacío o no se pudo leer.")
        return
    except FileNotFoundError:
        print(f"Error: El archivo {input_file} no fue encontrado.")
        return
        
    print(f"Leídas {len(df)} filas.")

    # Verificar que las columnas para pivotar existen y son numéricas
    if not all(col in df.columns and pd.api.types.is_numeric_dtype(df[col]) for col in PIVOT_VALUE_COLS):
        print(f"Error: Las columnas de valor {PIVOT_VALUE_COLS} no son numéricas.")
        return
        
    if not all(col in df.columns for col in PIVOT_INDEX_COLS + [PIVOT_COLUMN_COL]):
        print(f"Error: Faltan columnas de índice o pivot {PIVOT_INDEX_COLS + [PIVOT_COLUMN_COL]}.")
        return

    try:
        # --- Paso Clave: Pivotar la tabla ---
        # Queremos una fila por timestamp, con columnas para cada ancla
        # Usaremos la 'FilteredDistance(cm)' y 'RSSI(dBm)'
        df_pivot = df.pivot_table(
            index=PIVOT_INDEX_COLS, 
            columns=PIVOT_COLUMN_COL, 
            values=PIVOT_VALUE_COLS
        )

        # Aplanar los nombres de las columnas (e.g., ('FilteredDistance(cm)', 10) -> 'Dist_10')
        df_pivot.columns = [f'{val.replace("(cm)","").replace("(dBm)","")}_{int(col)}' for val, col in df_pivot.columns]
        df_pivot.reset_index(inplace=True)

        print(f"Datos pivotados. {len(df_pivot)} timestamps únicos.")
        print("Primeras filas pivotadas:")
        print(df_pivot.head())

        # --- Calcular Posición para cada Timestamp ---
        positions_x = []
        positions_y = []
        positions_z = []
        print("Calculando posiciones...")
        
        anchor_ids_available = sorted([aid for aid in anchor_positions_map.keys()]) # IDs de anclas con posición conocida
        
        for index, row in df_pivot.iterrows():
            responding_distances = {}
            responding_positions = {}
            num_valid_anchors = 0
            
            for anchor_id in anchor_ids_available:
                dist_col = f'FilteredDistance_{anchor_id}'
                # Verificar si la columna de distancia existe y no es NaN
                if dist_col in row and pd.notna(row[dist_col]):
                     dist_m = row[dist_col] / 100.0 # Convertir a metros
                     if dist_m > 0.01: # Considerar distancia válida si es > 1cm
                         responding_distances[anchor_id] = dist_m
                         responding_positions[anchor_id] = anchor_positions_map[anchor_id]
                         num_valid_anchors += 1

            # Calcular posición si hay suficientes anclas válidas
            if num_valid_anchors >= 3:
                pos_3d = multilateration_3d(responding_distances, responding_positions)
                if pos_3d is not None:
                    positions_x.append(pos_3d[0])
                    positions_y.append(pos_3d[1])
                    positions_z.append(pos_3d[2])
                else:
                    positions_x.append(np.nan)
                    positions_y.append(np.nan)
                    positions_z.append(np.nan)
            else:
                positions_x.append(np.nan)
                positions_y.append(np.nan)
                positions_z.append(np.nan)

        # Añadir columnas de posición al DataFrame
        df_pivot['Position_X'] = positions_x
        df_pivot['Position_Y'] = positions_y
        df_pivot['Position_Z'] = positions_z
        
        print("Cálculo de posiciones finalizado.")
        print("Primeras filas con posición:")
        print(df_pivot[['Timestamp(ms)', 'TagID', 'Position_X', 'Position_Y', 'Position_Z']].head())

        # --- Fin del post-procesado --- 

        # Guardar el DataFrame procesado
        try:
            df_pivot.to_csv(output_file, index=False, float_format='%.4f')
            print(f"Archivo procesado y enriquecido guardado en: {output_file}")
        except Exception as e:
            print(f"Error al guardar el archivo procesado: {e}")

    except KeyError as e:
         print(f"Error de clave al pivotar o acceder a columnas: {e}. Verifica los nombres de columna y los IDs de ancla en los datos.")
    except Exception as e:
        import traceback
        print(f"Error inesperado al pivotar o procesar: {e}")
        traceback.print_exc()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Post-procesa un archivo CSV de logs UWB, pivotando y calculando posición 3D.')
    parser.add_argument('--input', required=True, help='Ruta al archivo CSV de entrada (log crudo).')
    parser.add_argument('--output', required=True, help='Ruta para guardar el archivo CSV procesado (con posiciones).')
    # Opcional: añadir argumento para especificar archivo de config de anclas
    # parser.add_argument('--anchors', default=ANCHOR_CONFIG_FILE, help='Ruta al archivo JSON de configuración de anclas.') 
    args = parser.parse_args()

    # Llamar a la función principal
    process_uwb_log(args.input, args.output)
