import streamlit as st
import sqlite3
import pandas as pd
import plotly.graph_objects as go
from plotly.subplots import make_subplots
import os
from scipy import signal
import numpy as np
from circular_buffer_math import calculate_95th_percentile_series

# --- Configuration ---
DB_FILE = os.environ.get('GRIND_DB_PATH', '../database/grinder_data.db')
TOLERANCE_G = 0.05  # Actual grind accuracy tolerance
PROFILE_MAP = {0: "SINGLE", 1: "DOUBLE", 2: "CUSTOM"}
MODE_MAP = {0: "WEIGHT", 1: "TIME"}
TERMINATION_REASON_MAP = {
    0: "COMPLETE",
    1: "TIMEOUT",
    2: "OVERSHOOT",
    3: "MAX_PULSES",
    255: "UNKNOWN"
}

# --- Database Check ---
if not os.path.exists(DB_FILE):
    st.error(f"Database file not found! Please ensure it is located at: {DB_FILE}")
    st.info("If you haven't created the database yet, please run the `grinder export` command from your terminal.")
    st.stop()

# --- Optimized Data Loading ---
@st.cache_data
def load_session_list():
    """Loads just the list of sessions from the database."""
    with sqlite3.connect(DB_FILE) as conn:
        sessions = pd.read_sql_query("SELECT * FROM grind_sessions", conn)
    return sessions

@st.cache_data
def load_session_details(session_id):
    """Loads all event and measurement data for a single session ID."""
    with sqlite3.connect(DB_FILE) as conn:
        # Added ORDER BY to ensure data is always time-sorted on load
        events = pd.read_sql_query(f"SELECT * FROM grind_events WHERE session_id = {session_id} ORDER BY timestamp_ms", conn)
        measurements = pd.read_sql_query(f"SELECT * FROM grind_measurements WHERE session_id = {session_id} ORDER BY timestamp_ms", conn)
    # Clean up whitespace in phase names to prevent filtering issues
    if 'phase_name' in events.columns:
        events['phase_name'] = events['phase_name'].str.strip()
    if 'phase_name' in measurements.columns:
        measurements['phase_name'] = measurements['phase_name'].str.strip()
    return events, measurements

@st.cache_data
def load_all_details():
    """Loads all event and measurement data for multi-session analysis."""
    with sqlite3.connect(DB_FILE) as conn:
        # Added ORDER BY for consistency
        events = pd.read_sql_query("SELECT * FROM grind_events ORDER BY session_id, timestamp_ms", conn)
        measurements = pd.read_sql_query("SELECT * FROM grind_measurements ORDER BY session_id, timestamp_ms", conn)
    # Clean up whitespace in phase names to prevent filtering issues
    if 'phase_name' in events.columns:
        events['phase_name'] = events['phase_name'].str.strip()
    if 'phase_name' in measurements.columns:
        measurements['phase_name'] = measurements['phase_name'].str.strip()
    return events, measurements

# --- Helper Functions ---
def get_phase_specific_hover_data(events_df):
    """
    Creates phase-specific hover templates and custom data for a group of events.
    This function processes an entire DataFrame of events to generate the necessary
    data structures for efficient plotting with Plotly.
    """
    if events_df.empty:
        return [], ""

    phase = events_df['phase_name'].iloc[0]
    custom_data = []
    template = ""

    base_cols = ['phase_name', 'duration_ms', 'start_weight', 'end_weight']

    if phase in ['TARING', 'TARE_CONFIRM']:
        template = ("<b>%{customdata[0]}</b><br>"
                   "Duration: %{customdata[1]} ms<br>"
                   "Start Weight: %{customdata[2]:.3f}g<br>"
                   "End Weight: %{customdata[3]:.3f}g<extra></extra>")
        custom_data = events_df[base_cols].values.tolist()

    elif phase == 'PREDICTIVE':
        template = ("<b>%{customdata[0]}</b><br>"
                   "Duration: %{customdata[1]} ms<br>"
                   "Yield: %{customdata[4]:.2f}g<br>"
                   "Start: %{customdata[2]:.3f}g → End: %{customdata[3]:.2f}g<br>"
                   "Motor Stop Target: %{customdata[5]:.2f}g<br>"
                   "Grind Latency: %{customdata[6]} ms<extra></extra>")
        events_df['yield'] = events_df['end_weight'] - events_df['start_weight']
        custom_cols = base_cols + ['yield', 'motor_stop_target_weight', 'grind_latency_ms']
        custom_data = events_df[custom_cols].values.tolist()

    elif phase == 'PULSE_DECISION':
        template = ("<b>%{customdata[0]}</b><br>"
                   "Duration: %{customdata[1]} ms<br>"
                   "Decision Point<br>"
                   "Weight: %{customdata[2]:.3f}g → %{customdata[3]:.3f}g<extra></extra>")
        custom_data = events_df[base_cols].values.tolist()

    elif phase == 'PULSE_EXECUTE':
        template = ("<b>%{customdata[0]} #%{customdata[4]}</b><br>"
                   "Pulse Duration: %{customdata[5]} ms<br>"
                   "Phase Duration: %{customdata[1]} ms<br>"
                   "Weight: %{customdata[2]:.3f}g → %{customdata[3]:.3f}g<extra></extra>")
        custom_cols = base_cols + ['pulse_attempt_number', 'pulse_duration_ms']
        custom_data = events_df[custom_cols].values.tolist()

    elif phase == 'PULSE_SETTLING':
        template = ("<b>%{customdata[0]}</b><br>"
                   "Settling Duration: %{customdata[5]} ms<br>"
                   "Phase Duration: %{customdata[1]} ms<br>"
                   "Yield: %{customdata[4]:.3f}g<br>"
                   "Start: %{customdata[2]:.3f}g → End: %{customdata[3]:.3f}g<extra></extra>")
        events_df['yield'] = events_df['end_weight'] - events_df['start_weight']
        custom_cols = base_cols + ['yield', 'settling_duration_ms']
        custom_data = events_df[custom_cols].values.tolist()

    elif phase == 'FINAL_SETTLING':
        template = ("<b>%{customdata[0]}</b><br>"
                   "Duration: %{customdata[1]} ms<br>"
                   "Final Yield: %{customdata[4]:.3f}g<br>"
                   "Start: %{customdata[2]:.3f}g → End: %{customdata[3]:.3f}g<extra></extra>")
        events_df['yield'] = events_df['end_weight'] - events_df['start_weight']
        custom_cols = base_cols + ['yield']
        custom_data = events_df[custom_cols].values.tolist()

    else:  # Default for any other phases
        template = ("<b>%{customdata[0]}</b><br>"
                   "Duration: %{customdata[1]} ms<br>"
                   "Start: %{customdata[2]:.3f}g<br>"
                   "End: %{customdata[3]:.3f}g<extra></extra>")
        custom_data = events_df[base_cols].values.tolist()

    return custom_data, template

def get_staggered_y_positions(events_df, y_max):
    """Calculates staggered y-positions to prevent label overlap."""
    if y_max is None or pd.isna(y_max): y_max = 10
    
    positions = []
    levels = [y_max * 1.05, y_max * 1.20, y_max * 1.12, y_max * 1.27]
    last_x = -np.inf
    level_idx = 0
    time_threshold = (events_df['timestamp_ms'].max() - events_df['timestamp_ms'].min()) / 15 if not events_df.empty else 1000
    
    for _, event in events_df.iterrows():
        if (event['timestamp_ms'] - last_x) < time_threshold:
            level_idx = (level_idx + 1) % len(levels)
        else:
            level_idx = 0
        positions.append(levels[level_idx])
        last_x = event['timestamp_ms']
    return positions

def add_motor_on_rects(fig, measurements_df):
    """
    REFACTORED: Adds background rectangles for motor-on periods efficiently.
    Instead of adding one shape per data point, this function finds contiguous
    blocks of time where the motor was on and adds one large rectangle for each block.
    This significantly reduces the number of objects in the Plotly figure, boosting performance.
    """
    if 'motor_is_on' not in measurements_df.columns or measurements_df.empty:
        return fig

    df = measurements_df.sort_values('timestamp_ms')
    motor_on_periods = df[df['motor_is_on'] == 1]

    if motor_on_periods.empty:
        return fig

    # Calculate time difference between consecutive measurements
    time_diff = motor_on_periods['timestamp_ms'].diff()
    
    # Heuristic to identify a new block: if the time gap is more than 3x the median gap,
    # it's likely a separate motor activation.
    median_interval = time_diff.median()
    if pd.isna(median_interval) or median_interval == 0:
        median_interval = 50 # A reasonable fallback for sparse data
    
    # A new block starts where the time difference is large
    block_starts = time_diff > (3 * median_interval)
    # The first entry is always the start of a block
    block_starts.iloc[0] = True
    
    motor_on_periods['block_id'] = block_starts.cumsum()

    # Group by these contiguous blocks and draw one rectangle per block
    for _, group in motor_on_periods.groupby('block_id'):
        start_time = group['timestamp_ms'].iloc[0]
        end_time = group['timestamp_ms'].iloc[-1]
        
        # Extend the rectangle to the next measurement time for better visualization
        last_interval = time_diff.median() if pd.notna(time_diff.median()) else 50
        
        fig.add_shape(
            type="rect",
            x0=start_time, x1=end_time + last_interval,
            y0=0, y1=1,
            xref="x", yref="paper",
            fillcolor="DarkSlateBlue", opacity=0.3,
            layer="below", line_width=0
        )
    return fig

def create_phase_chart(measurements, title, full_session_measurements, flow_rate_col_name, weight_col_name='weight_grams', target_line=None, target_line_text="Target", events_to_mark=None):
    """Creates a standardized time series chart for a specific phase."""
    fig = make_subplots(specs=[[{"secondary_y": True}]])
    
    fig = add_motor_on_rects(fig, full_session_measurements)

    fig.add_trace(go.Scatter(x=measurements['timestamp_ms'], y=measurements[weight_col_name], 
                            mode='lines', name='Weight', line=dict(color='royalblue', width=2)), secondary_y=False)
    fig.add_trace(go.Scatter(x=measurements['timestamp_ms'], y=measurements[flow_rate_col_name], 
                            mode='lines', name='Flow Rate', line=dict(color='darkgreen', width=1)), secondary_y=True)
    
    if target_line is not None:
        fig.add_hline(y=target_line, line_dash="dash", line_color="salmon",
                      annotation_text=target_line_text, annotation_position="bottom right", secondary_y=False)
    
    if events_to_mark is not None and not events_to_mark.empty:
        plot_event_markers(fig, events_to_mark, measurements, weight_col_name=weight_col_name)

    if not measurements.empty:
        x_min, x_max = measurements['timestamp_ms'].min(), measurements['timestamp_ms'].max()
        x_buffer = max((x_max - x_min) * 0.05, 50)
        fig.update_xaxes(range=[x_min - x_buffer, x_max + x_buffer])

    fig.update_layout(title_text=title, template="plotly_white", hovermode="x unified", legend=dict(yanchor="top", y=0.99, xanchor="left", x=0.01))
    fig.update_xaxes(title_text="Time (milliseconds)")
    fig.update_yaxes(title_text="Weight (grams)", secondary_y=False)
    fig.update_yaxes(title_text="Flow Rate (g/s)", secondary_y=True, showgrid=False)
    return fig

def plot_event_markers(fig, events_df, measurements_df, weight_col_name='weight_grams'):
    """
    REFACTORED: Plots event markers efficiently.
    Instead of adding a new trace for each individual event, this function groups events
    by their phase name. It then adds only ONE trace per phase, containing all markers
    for that phase. This dramatically reduces the number of traces and improves chart performance.
    """
    if events_df.empty: return

    y_max = measurements_df[weight_col_name].max() if not measurements_df.empty and weight_col_name in measurements_df.columns else 10
    y_positions = get_staggered_y_positions(events_df, y_max)
    events_df['y_pos'] = y_positions

    # Add all vertical lines in one loop (this is minor, but cleaner)
    for t in events_df['timestamp_ms']:
        fig.add_vline(x=t, line_dash="dot", line_color="grey", opacity=0.5)
    
    # Group by phase to add markers and hover templates in batches
    for phase_name, group in events_df.groupby('phase_name'):
        custom_data, hover_template = get_phase_specific_hover_data(group)
        
        fig.add_trace(go.Scatter(
            x=group['timestamp_ms'], y=group['y_pos'],
            mode='markers',
            marker=dict(symbol='triangle-down', color='grey', size=10),
            customdata=custom_data,
            hovertemplate=hover_template,
            name=phase_name,
            legendgroup='events',
            showlegend=False
        ), secondary_y=False)
        
    # Add annotations (looping here is acceptable as the number of annotations is small)
    for _, event in events_df.iterrows():
        fig.add_annotation(
            x=event['timestamp_ms'], y=event['y_pos'], text=event['phase_name'],
            showarrow=False, textangle=-45, yanchor="bottom", yshift=5,
            font=dict(size=10, color="grey")
        )

# --- Main App ---
st.set_page_config(page_title="Grind Performance Analysis", layout="wide")
st.title("Grind-By-Weight Performance Analysis")

# Load only the session list initially for speed
sessions_df = load_session_list()

# --- Sidebar ---
st.sidebar.header("Analysis Mode")
analysis_mode = st.sidebar.radio("Choose analysis type", 
                                 ["Single Session", "Multi-Session Analysis"],
                                 help="Single Session: Detailed analysis of one grind session with time series, phase breakdowns, and vibration analysis. Multi-Session: Comparative analysis of multiple sessions focusing on pulse effectiveness and correlation statistics.")

if sessions_df.empty:
    st.warning("No sessions found in the database.")
    st.stop()

# Normalise expected columns for backward-compatible databases
default_columns = {
    'grind_mode': 0,
    'target_time_ms': 0,
    'time_error_ms': 0,
    'start_weight': 0.0,
    'max_pulse_attempts': 0,
    'termination_reason': 255,
    'latency_to_coast_ratio': 0.0,
    'flow_rate_threshold': 0.0,
    'pulse_duration_large': 0.0,
    'pulse_duration_medium': 0.0,
    'pulse_duration_small': 0.0,
    'pulse_duration_fine': 0.0,
    'large_error_threshold': 0.0,
    'medium_error_threshold': 0.0,
    'small_error_threshold': 0.0,
    'schema_version': 1,
    'checksum': 0,
    'session_size_bytes': 0
}
for column, default_value in default_columns.items():
    if column not in sessions_df.columns:
        sessions_df[column] = default_value

sessions_df['mode_name'] = sessions_df['grind_mode'].map(MODE_MAP).fillna('UNKNOWN')
sessions_df['time_error_ms'] = sessions_df['time_error_ms'].fillna(0)
sessions_df['target_time_ms'] = sessions_df['target_time_ms'].fillna(0)
sessions_df['time_error_s'] = sessions_df['time_error_ms'] / 1000.0
sessions_df['target_time_s'] = sessions_df['target_time_ms'] / 1000.0
sessions_df['total_motor_on_time_s'] = sessions_df['total_motor_on_time_ms'] / 1000.0
sessions_df['termination_reason_name'] = sessions_df['termination_reason'].map(TERMINATION_REASON_MAP).fillna('UNKNOWN')

# Ensure error is calculated as final - target for display purposes,
# which handles old data where the DB value might be inverted.
sessions_df['error_grams'] = sessions_df['final_weight'] - sessions_df['target_weight']
sessions_df.loc[sessions_df['mode_name'] != 'WEIGHT', 'error_grams'] = 0.0

sessions_df['profile_name'] = sessions_df['profile_id'].map(PROFILE_MAP)

mode_filter_options = ["All", "Weight", "Time"]
mode_filter = st.sidebar.selectbox("Grind mode", mode_filter_options, index=0)
if mode_filter == "Weight":
    sessions_df = sessions_df[sessions_df['mode_name'] == 'WEIGHT']
elif mode_filter == "Time":
    sessions_df = sessions_df[sessions_df['mode_name'] == 'TIME']

if sessions_df.empty:
    st.warning("No sessions match the selected filters.")
    st.stop()

def format_session_display(row):
    mode = row.get('mode_name', 'WEIGHT')
    if mode == 'TIME':
        target_time_s = row.get('target_time_s', 0.0)
        delta_s = row.get('time_error_s', 0.0)
        return f"#{row['session_id']} - {row['profile_name']} - {target_time_s:.1f}s ({delta_s:+.2f}s)"
    target_weight = row.get('target_weight', 0.0)
    error_grams = row.get('error_grams', 0.0)
    return f"#{row['session_id']} - {row['profile_name']} - {target_weight:.1f}g ({error_grams:+.2f}g)"

sessions_df['display_name'] = sessions_df.apply(format_session_display, axis=1)

if analysis_mode == "Single Session":
    st.sidebar.header("Grind Session Selector")
    selected_display_name = st.sidebar.selectbox(
        "Select a session to analyze:",
        options=sessions_df['display_name'].unique(),
        index=len(sessions_df['display_name'].unique()) - 1
    )
    selected_session_id = int(selected_display_name.split(' - ')[0].replace('#',''))

    # --- Load detailed data only for the selected session ---
    session_data = sessions_df[sessions_df['session_id'] == selected_session_id].iloc[0]
    session_events, session_measurements = load_session_details(selected_session_id)
    
    # --- Single Taring Control (define early so it can be used in filters) ---
    include_taring_process = st.sidebar.checkbox("Include taring process", value=False,
                                                help="Show TARING and TARE_CONFIRM phases in all charts and data. When unchecked, completely removes taring phases from analysis.")
    
    # --- Sidebar Filters ---
    st.sidebar.header("Chart Filters")
    st.sidebar.markdown("**Select events to display:**")
    all_phases = sorted(session_events['phase_name'].unique())
    
    # Filter out taring phases if taring process is excluded
    if not include_taring_process:
        all_phases = [phase for phase in all_phases if phase not in ['TARING', 'TARE_CONFIRM']]
    
    selected_phases = []
    for phase in all_phases:
        default_val = phase not in ['PULSE_DECISION', 'PULSE_SETTLING']
        phase_descriptions = {
            'TARING': 'Zeroing scale before grind',
            'TARE_CONFIRM': 'Confirming tare completion',
            'PREDICTIVE': 'Main grinding with flow prediction',
            'PULSE_DECISION': 'Deciding if correction needed',
            'PULSE_EXECUTE': 'Executing precision pulse',
            'PULSE_SETTLING': 'Waiting for weight to settle',
            'FINAL_SETTLING': 'Final weight stabilization'
        }
        help_text = phase_descriptions.get(phase, f"Phase: {phase}")
        if st.sidebar.checkbox(phase, value=default_val, key=f"phase_{phase}", help=help_text):
            selected_phases.append(phase)
    
    st.sidebar.header("Flow Rate Smoothing")
    smoothing_option = st.sidebar.radio(
        "Smoothing window:",
        ('None', '100 ms', '500 ms', '1000 ms', '1500 ms'),
        index=2, # Default to 500ms
        key='smoothing_radio',
        help="Apply rolling average to flow rate calculations. Larger windows reduce noise but increase lag. Flow rate = weight_delta / time_delta."
    )
    
    # --- Data Processing for Single Session ---
    original_measurements = session_measurements.copy()  # Keep completely unfiltered
    original_events = session_events.copy()              # Keep completely unfiltered
    
    # Remove internal controller phases that shouldn't be displayed
    internal_phases_to_remove = ['IDLE', 'SETUP']
    session_events = session_events.query("phase_name not in @internal_phases_to_remove")
    session_measurements = session_measurements.query("phase_name not in @internal_phases_to_remove")
    
    # Remove taring process if not included
    if not include_taring_process:
        tare_phases_to_remove = ['TARING', 'TARE_CONFIRM']
        session_events = session_events.query("phase_name not in @tare_phases_to_remove")
        session_measurements = session_measurements.query("phase_name not in @tare_phases_to_remove")
    
    # FIX: Sorting by timestamp is crucial for time-based rolling window calculations 
    # to work correctly. Without this, smoothed data can be misaligned.
    session_measurements.sort_values('timestamp_ms', inplace=True, ignore_index=True)

    # Apply flow rate smoothing if selected
    flow_rate_col = 'flow_rate_g_per_s'
    if smoothing_option != 'None':
        window_size_ms = int(smoothing_option.split(' ')[0])
        flow_rate_col = f'flow_rate_smoothed_{window_size_ms}ms'
        # Use a datetime index for the time-based rolling window
        temp_df = session_measurements.set_index(pd.to_datetime(session_measurements['timestamp_ms'], unit='ms'))
        smoothed_series = temp_df['flow_rate_g_per_s'].rolling(window=f'{window_size_ms}ms').mean()
        session_measurements[flow_rate_col] = smoothed_series.values

    weight_col = 'weight_grams'

    # --- Single Session Display ---
    tab1, tab2, tab3, tab4, tab5 = st.tabs(["Overall Analysis", "Predictive Phase", "Pulse Phase", "Vibration Analysis", "Controller Performance"])

    with tab1:
        st.header(f"Grind Session Summary: {selected_display_name}")
        
        # Calculate total grind time (from predictive start to final settle end)
        grind_time_s = 0
        predictive_start_event = session_events.query("phase_name == 'PREDICTIVE'")
        if not predictive_start_event.empty:
            start_time = predictive_start_event['timestamp_ms'].min()
            
            # Find the end of the last settling phase
            settling_phases = ['FINAL_SETTLING', 'PULSE_SETTLING']
            last_settle_event = session_events[session_events['phase_name'].isin(settling_phases)]
            if not last_settle_event.empty:
                end_time = (last_settle_event['timestamp_ms'] + last_settle_event['duration_ms']).max()
                grind_time_s = (end_time - start_time) / 1000.0

        c1, c2, c3, c4, c5, c6 = st.columns(6)
        avg_meas_per_sec = len(session_measurements) / grind_time_s if grind_time_s > 0 else 0
        if mode_name == 'TIME':
            target_time_s = session_data.get('target_time_s', 0.0)
            time_error_s = session_data.get('time_error_s', 0.0)
            c1.metric("Target Time (s)", f"{target_time_s:.2f}", f"{time_error_s:+.2f} s",
                      delta_color='inverse', help="Positive delta indicates the grind ran longer than the target time.")
            c2.metric("Motor On Time (s)", f"{session_data.get('total_motor_on_time_s', 0.0):.2f}",
                     help="Total motor runtime recorded for the session.")
            c3.metric("Session Duration (s)", f"{session_data.get('total_time_ms', 0) / 1000.0:.2f}",
                     help="Overall session length including settling phases.")
            c4.metric("Termination", session_data.get('termination_reason_name', session_data['result_status']),
                     help="Termination reason reported by the firmware.")
            c5.metric("Final Weight (g)", f"{session_data['final_weight']:.2f}",
                     help="Settled weight captured at session end.")
            c6.metric("Data Resolution", f"{avg_meas_per_sec:.1f} meas/sec",
                     help="Sampling frequency during the grind. Higher values indicate more precise weight logging.")
        else:
            # Set delta color based on tolerance, not just positive/negative value.
            is_within_tolerance = abs(session_data['error_grams']) < TOLERANCE_G
            is_negative = session_data['error_grams'] < 0
            error_color = "inverse" if (is_within_tolerance and is_negative) or (not is_within_tolerance and not is_negative) else "normal"
            c1.metric("Target (g)", f"{session_data['target_weight']:.2f}", f"{session_data['error_grams']:+.2f} g",
                      delta_color=error_color, help="Green if |error| < 0.05g, Red otherwise.")
            c2.metric("Final (g)", f"{session_data['final_weight']:.2f}", 
                     help="Final settled weight after grind completion. Measured during FINAL_SETTLING phase when motor is off and vibrations have stopped.")
            c3.metric("Grind Time (s)", f"{grind_time_s:.1f}", 
                     help="Total active grind time from start of PREDICTIVE phase to end of final settling. Excludes taring and setup time.")
            c4.metric("Result", session_data['result_status'],
                     help="Final grind outcome: COMPLETE (within tolerance), TIMEOUT (exceeded max time), OVERSHOOT (too much coffee), or other controller states.")
            c5.metric("Pulse Count", session_data['pulse_count'],
                     help="Number of precision pulse corrections executed during grind. Max 10 pulses per session as defined by USER_GRIND_MAX_PULSE_ATTEMPTS.")
            c6.metric("Data Resolution", f"{avg_meas_per_sec:.1f} meas/sec",
                     help="Data sampling frequency during grind. Higher values indicate more precise weight measurements. Controlled by load cell update interval and logging frequency.")

        st.header("Time Series Analysis with Event Annotations")

        chart_measurements = session_measurements.copy()
        chart_events = session_events.copy()
        chart_original = original_measurements.copy()
        
        fig = make_subplots(specs=[[{"secondary_y": True}]])
        fig = add_motor_on_rects(fig, chart_original)
        
        fig.add_trace(go.Scatter(x=chart_measurements['timestamp_ms'], y=chart_measurements[weight_col],
                                 mode='lines', name='Weight', line=dict(color='royalblue', width=2)),
                      secondary_y=False)
        fig.add_trace(go.Scatter(x=chart_measurements['timestamp_ms'], y=chart_measurements[flow_rate_col],
                                 mode='lines', name=f'Flow Rate ({smoothing_option})', line=dict(color='darkgreen', width=1)),
                      secondary_y=True)
        if mode_name == 'WEIGHT':
            fig.add_hline(y=session_data['target_weight'], line_dash="dash", line_color="salmon",
                          annotation_text="Target (g)", annotation_position="bottom right", secondary_y=False)
        
        display_events = chart_events[chart_events['phase_name'].isin(selected_phases)]
        plot_event_markers(fig, display_events, chart_measurements, weight_col_name=weight_col)

        # Add marker for start of flow detection
        predictive_event = chart_events.query("phase_name == 'PREDICTIVE'")
        if mode_name == 'WEIGHT' and not predictive_event.empty:
            start_time = predictive_event['timestamp_ms'].iloc[0]
            latency = predictive_event['grind_latency_ms'].iloc[0]
            detection_time = start_time + latency
            
            # Find the flow rate at that specific time for plotting
            flow_at_detection = np.interp(detection_time, chart_measurements['timestamp_ms'], chart_measurements[flow_rate_col])

            fig.add_trace(go.Scatter(
                x=[detection_time],
                y=[flow_at_detection],
                mode='markers',
                marker=dict(symbol='x-thin-open', color='purple', size=10, line=dict(width=2)),
                name='Start of flow detected',
                hovertemplate='<b>Start of flow detected</b><br>Time: %{x} ms<br>Flow Rate: %{y:.2f} g/s<extra></extra>'
            ), secondary_y=True)

        fig.update_layout(title=f"Grind Profile for Session #{selected_session_id}",
                          xaxis_title="Time (milliseconds)", yaxis_title="Weight (grams)",
                          template="plotly_white", hovermode="x unified", legend=dict(yanchor="top", y=0.99, xanchor="left", x=0.01))
        fig.update_yaxes(title_text="Weight (g)", secondary_y=False)
        fig.update_yaxes(title_text="Flow Rate (g/s)", secondary_y=True, showgrid=False)
        st.plotly_chart(fig, use_container_width=True)

    with tab2:
        st.header("Predictive Phase Analysis")
        if mode_name != 'WEIGHT':
            st.info("Predictive analysis is only available for grind-by-weight sessions.")
        else:
            predictive_events = session_events.query("phase_name == 'PREDICTIVE'")

            if not predictive_events.empty:
                phase_measurements = session_measurements.query("phase_name == 'PREDICTIVE'")
                events_to_mark = predictive_events.copy()

                predictive_end_time = predictive_events.iloc[0]['timestamp_ms'] + predictive_events.iloc[0]['duration_ms']
                first_settle_event = session_events.query(
                    "phase_name == 'PULSE_SETTLING' and timestamp_ms >= @predictive_end_time"
                ).sort_values('timestamp_ms').iloc[:1]

                if not first_settle_event.empty:
                    settle_start_time = first_settle_event.iloc[0]['timestamp_ms']
                    settle_end_time = settle_start_time + first_settle_event.iloc[0]['duration_ms']
                    settle_measurements = session_measurements.query("@settle_start_time <= timestamp_ms <= @settle_end_time")
                    phase_measurements = pd.concat([phase_measurements, settle_measurements]).sort_values('timestamp_ms').drop_duplicates()
                    events_to_mark = pd.concat([events_to_mark, first_settle_event])
                
                # --- Resample and Calculate 95th Percentile Flow Rate ---
                if not phase_measurements.empty:
                    # Convert timestamp to a datetime index for resampling
                    df_resample = phase_measurements.set_index(pd.to_datetime(phase_measurements['timestamp_ms'], unit='ms'))
                    # Resample to 100ms (10Hz) and take the last known value in each interval.
                    # This aligns our analysis with the actual sample rate of the hardware.
                    resampled_df = df_resample.resample('100ms').last().dropna()

                    # Drop the old 'timestamp_ms' column before resetting the index to avoid a name collision.
                    if 'timestamp_ms' in resampled_df.columns:
                        resampled_df = resampled_df.drop(columns=['timestamp_ms'])
                    resampled_measurements = resampled_df.reset_index().rename(columns={'index': 'timestamp_ms'})

                    # Convert the pandas Timestamp object back to an integer (milliseconds) before calculations
                    resampled_measurements['timestamp_ms'] = resampled_measurements['timestamp_ms'].astype(np.int64) // 1_000_000

                    # EXPERIMENTAL: Test fixed window sizes (2500ms total, 300ms sub-window, 100ms step)
                    resampled_measurements = calculate_95th_percentile_series(
                        resampled_measurements, 
                        window_ms=2500, 
                        sub_window_ms=300, 
                        step_ms=100
                    )

                predictive_data = predictive_events.iloc[0]

                st.subheader("Key Metrics")
                c1, c2, c3, c4 = st.columns(4)
                
                # Calculate weight added using the end of the first settle phase for accuracy
                if not first_settle_event.empty:
                    # Use the end weight from the first settle event and start weight from predictive event
                    settled_weight = first_settle_event.iloc[0]['end_weight']
                    yield_val = settled_weight - predictive_data['start_weight']
                    c1.metric("Total Yield (g)", f"{yield_val:.2f}", 
                              help="Weight at the end of the first settle phase minus the weight at the start of the predictive phase.")
                else:
                    # Fallback to using only the predictive event if no settle event is found
                    yield_val = predictive_data['end_weight'] - predictive_data['start_weight']
                    c1.metric("Total Yield (g)", f"{yield_val:.2f}", help="Yield from the predictive phase only (settling data not found).")

                # Calculate and display the actual weight target for the predictive phase
                motor_stop_offset = predictive_data['motor_stop_target_weight']
                motor_stop_target_weight = session_data['target_weight'] - motor_stop_offset
                c2.metric("Motor Stop Target", f"{motor_stop_target_weight:.2f} g", 
                          f"Stop Offset: {motor_stop_offset:.2f}g", delta_color="off",
                          help="The weight at which the motor was commanded to stop. (Target (g) - Motor Stop Offset).")
                
                # Display the latency-to-coast ratio used in this session
                latency_coast_ratio = session_data.get('latency_to_coast_ratio', 'N/A')
                c3.metric("Latency-to-Coast Ratio", f"{latency_coast_ratio:.2f}" if isinstance(latency_coast_ratio, float) else "N/A",
                          help="Ratio used to predict coasting time after motor stop. Higher ratios expect longer coasting. Used to calculate motor stop target: expected_coast_time = grind_latency * ratio.")

                c4.metric("Grind Latency (ms)", f"{predictive_data['grind_latency_ms']}",
                         help="Time from motor start until flow detection threshold reached (USER_GRIND_FLOW_DETECTION_THRESHOLD_GPS). Measures grinder startup lag and is used to predict coasting behavior.")


                if not phase_measurements.empty:
                    fig = create_phase_chart(phase_measurements, 'Predictive Phase & First Settle Details', 
                                           full_session_measurements=original_measurements,
                                           flow_rate_col_name=flow_rate_col,
                                           weight_col_name=weight_col,
                                           events_to_mark=events_to_mark)
                    
                    # Add a horizontal line for the predictive target weight
                    fig.add_hline(
                        y=motor_stop_target_weight, line_dash="dash", line_color="orange",
                        annotation_text="Motor Stop Target", annotation_position="bottom right",
                        secondary_y=False
                    )

                    if 'flow_rate_95p_cpp' in resampled_measurements.columns and not resampled_measurements['flow_rate_95p_cpp'].isna().all():
                        fig.add_trace(go.Scatter(
                            x=resampled_measurements['timestamp_ms'], 
                            y=resampled_measurements['flow_rate_95p_cpp'],
                            mode='lines', name='95th Pct. Flow Rate (2.5s/300ms/100ms)', line=dict(color='purple', width=2, dash='dot')
                        ), secondary_y=True)

                    # Add marker for start of flow detection
                    predictive_event = events_to_mark.query("phase_name == 'PREDICTIVE'")
                    if not predictive_event.empty:
                        start_time = predictive_event['timestamp_ms'].iloc[0]
                        latency = predictive_event['grind_latency_ms'].iloc[0]
                        detection_time = start_time + latency
                        
                        # Find the flow rate at that specific time for plotting
                        flow_at_detection = np.interp(detection_time, phase_measurements['timestamp_ms'], phase_measurements[flow_rate_col])

                        fig.add_trace(go.Scatter(
                            x=[detection_time],
                            y=[flow_at_detection],
                            mode='markers',
                            marker=dict(symbol='x-thin-open', color='purple', size=10, line=dict(width=2)),
                            name='Start of flow detected',
                            hovertemplate='<b>Start of flow detected</b><br>Time: %{x} ms<br>Flow Rate: %{y:.2f} g/s<extra></extra>'
                        ), secondary_y=True)
                    
                    st.plotly_chart(fig, use_container_width=True)
            else:
                st.info("No predictive phase data found for this session.")

    with tab3:
        st.header("Pulse Phase Analysis")
        if mode_name != 'WEIGHT':
            st.info("Pulse analysis is only available for grind-by-weight sessions.")
        else:
            pulse_events = session_events[session_events['phase_name'].isin(['PULSE_EXECUTE', 'PULSE_SETTLING', 'PULSE_DECISION'])]
            summary = []
            pulse_executes = pulse_events[pulse_events['phase_name'] == 'PULSE_EXECUTE'].sort_values('timestamp_ms')

            # Get pulse flow rate from the predictive phase event
            predictive_event_data = session_events.query("phase_name == 'PREDICTIVE'")
            pulse_flow_rate = 0
            if not predictive_event_data.empty:
                # Use .get() for safety in case column doesn't exist in older DBs
                pulse_flow_rate = predictive_event_data.iloc[0].get('pulse_flow_rate', 0)

            for _, pulse in pulse_executes.iterrows():
                settle = pulse_events[(pulse_events['phase_name'] == 'PULSE_SETTLING') & (pulse_events['timestamp_ms'] > pulse['timestamp_ms'])]
                end_w = settle['end_weight'].iloc[0] if not settle.empty else pulse['end_weight']
                settle_t = settle['duration_ms'].iloc[0] if not settle.empty else 0
                
                pulse_yield = end_w - pulse['start_weight']
                expected_yield = (pulse['pulse_duration_ms'] / 1000.0) * pulse_flow_rate

                summary.append({"Pulse #": f"Pulse {pulse['pulse_attempt_number']}", "Duration (ms)": pulse['pulse_duration_ms'],
                                "Start Weight (g)": pulse['start_weight'], "End Weight (g)": end_w,
                                "Pulse Yield (g)": pulse_yield, 
                                "Expected Yield (g)": expected_yield,
                                "Settling Time (ms)": settle_t})

            if summary:
                summary_df = pd.DataFrame(summary)
                
                st.subheader("Key Metrics")
                total_pulse_weight = summary_df['Pulse Yield (g)'].sum()
                
                # Get pulse flow rate from the predictive phase event
                predictive_event_data = session_events.query("phase_name == 'PREDICTIVE'")
                pulse_flow_rate = 0
                if not predictive_event_data.empty:
                    # Use .get() for safety in case column doesn't exist in older DBs
                    pulse_flow_rate = predictive_event_data.iloc[0].get('pulse_flow_rate', 0)

                c1, c2, c3 = st.columns(3)
                c1.metric("Total Pulse Yield (g)", f"{total_pulse_weight:.2f}",
                         help="Total coffee weight added by all pulse corrections combined. Each pulse uses short bursts (50-400ms) to precisely reach target weight.")
                c2.metric("Number of Pulses", len(summary_df),
                         help="Count of PULSE_EXECUTE events. System stops pulsing when within tolerance or max attempts reached (USER_GRIND_MAX_PULSE_ATTEMPTS = 10).")
                c3.metric("Pulse Flow Rate (g/s)", f"{pulse_flow_rate:.3f}", 
                         help="95th percentile flow rate calculated during PREDICTIVE phase, stored in pulse_flow_rate field. Used to calculate pulse duration: duration = (error_grams / flow_rate).")


                st.subheader("Pulse Summary")
                st.dataframe(summary_df)
                
                col1, col2, col3 = st.columns(3)
                with col1:
                    st.subheader("Yield per Pulse")
                    fig_bar = go.Figure(data=[go.Bar(x=summary_df['Pulse #'], y=summary_df['Pulse Yield (g)'],
                                                     text=summary_df['Pulse Yield (g)'].apply(lambda x: f'{x:.2f}g'), textposition='auto')])
                    fig_bar.update_layout(title_text='Pulse Contribution', xaxis_title="Pulse", yaxis_title="Pulse Yield (g)")
                    st.plotly_chart(fig_bar, use_container_width=True)
                
                with col2:
                    st.subheader("Pulse Effectiveness")
                    fig_scatter = go.Figure(data=go.Scatter(
                        x=summary_df['Duration (ms)'], y=summary_df['Pulse Yield (g)'],
                        mode='markers', marker=dict(size=10), text=summary_df['Pulse #']
                    ))
                    fig_scatter.update_layout(title_text='Duration vs. Yield', xaxis_title="Pulse Duration (ms)", yaxis_title="Pulse Yield (g)")
                    st.plotly_chart(fig_scatter, use_container_width=True)

                with col3:
                    st.subheader("Prediction Accuracy")
                    fig_accuracy = go.Figure()
                    fig_accuracy.add_trace(go.Scatter(
                        x=summary_df['Expected Yield (g)'], y=summary_df['Pulse Yield (g)'],
                        mode='markers', marker=dict(size=10), text=summary_df['Pulse #'],
                        name='Pulses'
                    ))
                    # Add a 1:1 line for reference
                    min_val = min(summary_df['Expected Yield (g)'].min(), summary_df['Pulse Yield (g)'].min())
                    max_val = max(summary_df['Expected Yield (g)'].max(), summary_df['Pulse Yield (g)'].max())
                    fig_accuracy.add_trace(go.Scatter(
                        x=[min_val, max_val], y=[min_val, max_val],
                        mode='lines', line=dict(dash='dash', color='grey'), name='Perfect Prediction'
                    ))
                    fig_accuracy.update_layout(title_text='Expected vs. Actual Yield', xaxis_title="Expected Yield (g)", yaxis_title="Actual Pulse Yield (g)")
                    st.plotly_chart(fig_accuracy, use_container_width=True)

                pulse_measurements = session_measurements[session_measurements['phase_name'].isin(['PULSE_EXECUTE', 'PULSE_SETTLING', 'PULSE_DECISION'])]
                if not pulse_measurements.empty:
                    st.plotly_chart(create_phase_chart(pulse_measurements, 'Pulse & Settling Details', 
                                                       full_session_measurements=original_measurements,
                                                       flow_rate_col_name=flow_rate_col,
                                                       weight_col_name=weight_col,
                                                       events_to_mark=pulse_events), 
                                    use_container_width=True)
            else:
                st.info("No pulse phase data found for this session.")


    with tab4:
        st.header("Vibration Frequency Analysis")
        st.markdown("""
        This analysis examines the high-frequency "jitter" in the weight measurements during the predictive grinding phase while the motor is active.
        By applying a Fast Fourier Transform (FFT), we can identify the dominant frequencies of vibration from the motor and burrs.
        A strong, clear peak may indicate the primary operational frequency of the motor. This data helps optimize filtering algorithms and understand mechanical characteristics.
        
        **Technical Details**: Analysis uses detrended weight measurements from PREDICTIVE phase when motor_is_on=1. Sample rate varies based on controller frequency (typically 50Hz).
        """)

        predictive_motor_on_df = session_measurements[
            (session_measurements['phase_name'] == 'PREDICTIVE') &
            (session_measurements['motor_is_on'] == 1)
        ].copy()

        if len(predictive_motor_on_df) < 20:
            st.warning("Not enough data in the predictive phase with the motor on to perform vibration analysis.")
        else:
            time_ms = predictive_motor_on_df['timestamp_ms'].values
            duration_s = (time_ms[-1] - time_ms[0]) / 1000.0 if len(time_ms) > 1 else 0
            num_samples = len(time_ms)
            sampling_rate = num_samples / duration_s if duration_s > 0 else 0

            st.write(f"Analyzing **{num_samples}** data points over **{duration_s:.2f} seconds**. "
                     f"Average sampling rate: **{sampling_rate:.1f} Hz**.")

            raw_signal = predictive_motor_on_df['weight_grams'].values
            detrended_signal = signal.detrend(raw_signal)

            def calculate_fft(signal_data, sampling_rate):
                N = len(signal_data)
                if N == 0 or sampling_rate == 0:
                    return np.array([]), np.array([])
                yf = np.fft.fft(signal_data)
                xf = np.fft.fftfreq(N, 1 / sampling_rate)
                freqs = xf[:N//2]
                amps = 2.0/N * np.abs(yf[0:N//2])
                return freqs, amps

            freqs, amps = calculate_fft(detrended_signal, sampling_rate)
            
            # --- Chart 1: Raw Jitter Signal ---
            st.subheader("Vibration Signal (Time Domain)")
            st.markdown("This chart shows the raw vibration signal after removing the overall trend of increasing weight.")
            fig_jitter = go.Figure()
            fig_jitter.add_trace(go.Scatter(x=predictive_motor_on_df['timestamp_ms'], y=detrended_signal, mode='lines', name='Weight Jitter'))
            fig_jitter.update_layout(xaxis_title="Time (ms)", yaxis_title="Weight Fluctuation (g)")
            st.plotly_chart(fig_jitter, use_container_width=True)
            
            # --- Chart 2: Base FFT with Peak metric ---
            st.subheader("Raw Frequency Spectrum (FFT)")
            st.markdown("This chart displays the FFT of the original jitter signal, breaking it down into its constituent frequencies.")
            
            fig_fft_raw = go.Figure()
            fig_fft_raw.add_trace(go.Bar(x=freqs, y=amps, name='Original Signal'))

            if len(freqs) > 1:
                peak_idx = np.argmax(amps[1:]) + 1
                peak_freq = freqs[peak_idx]
                peak_amp = amps[peak_idx]
                fig_fft_raw.add_annotation(x=peak_freq, y=peak_amp, text=f"Peak: {peak_freq:.1f} Hz", showarrow=True, arrowhead=1)
                st.metric("Peak Vibration Frequency", f"{peak_freq:.1f} Hz",
                         help="Dominant vibration frequency detected in weight measurements. May correspond to motor RPM, burr resonance, or mechanical coupling frequencies. Useful for designing vibration filters.")
            
            fig_fft_raw.update_layout(title="Raw Signal Spectrum", xaxis_title="Frequency (Hz)", yaxis_title="Amplitude", xaxis_range=[0, sampling_rate / 2 if sampling_rate > 0 else 1])
            st.plotly_chart(fig_fft_raw, use_container_width=True)

            # --- Chart 3: IIR Filter ---
            st.markdown("---")
            st.subheader("IIR Filter Analysis")
            show_iir = st.checkbox("Show IIR Filtered Spectrum",
                                  help="Apply Infinite Impulse Response filter to smooth vibration data. IIR filters use feedback and are computationally efficient for real-time applications.")
            if show_iir:
                iir_alpha = st.slider("IIR Filter Alpha", 0.01, 0.99, 0.25, 
                                      help="Filter coefficient: y[n] = α*x[n] + (1-α)*y[n-1]. Lower α = more smoothing, higher α = less smoothing. Affects both cutoff frequency and phase response.")
                iir_b = [iir_alpha]
                iir_a = [1, iir_alpha - 1]
                filtered_iir = signal.lfilter(iir_b, iir_a, detrended_signal)
                freqs_iir, amps_iir = calculate_fft(filtered_iir, sampling_rate)
                
                fig_iir = go.Figure()
                fig_iir.add_trace(go.Bar(x=freqs_iir, y=amps_iir, name=f'IIR (α={iir_alpha:.2f})', marker_color='darkorange'))
                fig_iir.update_layout(title="IIR Filter Effect on Spectrum", xaxis_title="Frequency (Hz)", yaxis_title="Amplitude", xaxis_range=[0, sampling_rate / 2 if sampling_rate > 0 else 1])
                st.plotly_chart(fig_iir, use_container_width=True)

            # --- Chart 4: Notch Filter ---
            st.markdown("---")
            st.subheader("Notch Filter Analysis")
            show_notch = st.checkbox("Show Notch Filtered Spectrum", help="Apply a Biquad notch filter to remove a specific frequency.")
            if show_notch:
                col1, col2 = st.columns(2)
                with col1:
                    notch_freq = st.slider("Notch Frequency (Hz)", 0.1, 15.0, 0.2, 0.1,
                                           help="Center frequency to eliminate from signal. Set to dominant vibration frequency to remove motor noise. Must be less than Nyquist frequency (sample_rate/2).")
                with col2:
                    q_factor = st.slider("Q Factor", 1.0, 50.0, 5.0, 1.0,
                                         help="Filter selectivity: Q = center_freq / bandwidth. Higher Q = narrower notch, more selective. Lower Q = wider notch, affects adjacent frequencies.")

                fs = sampling_rate
                f_notch = notch_freq
                Q = q_factor
                
                fig_notch = go.Figure()
                if fs > 0 and f_notch < fs / 2:
                    b, a = signal.iirnotch(f_notch, Q, fs)
                    filtered_notch = signal.lfilter(b, a, detrended_signal)
                    freqs_notch, amps_notch = calculate_fft(filtered_notch, sampling_rate)
                    fig_notch.add_trace(go.Bar(x=freqs_notch, y=amps_notch, name=f'Notch Filter ({f_notch} Hz, Q={Q:.1f})', marker_color='darkgreen'))
                
                fig_notch.update_layout(title="Notch Filter Effect on Spectrum", xaxis_title="Frequency (Hz)", yaxis_title="Amplitude", xaxis_range=[0, sampling_rate / 2 if sampling_rate > 0 else 1])
                st.plotly_chart(fig_notch, use_container_width=True)

    with tab5:
        st.header("Controller Loop Performance")
        st.markdown("""
        This tab shows the performance of the main controller loop during each phase of the grind session.
        
        **Technical Details**: The GrindController runs on Core 0 at fixed intervals (SYS_TASK_GRIND_CONTROL_INTERVAL_MS = 20ms target).
        Loop count tracking measures actual performance vs. target frequency. Lower frequencies may indicate system load or blocking operations.
        """)

        if not session_events.empty and 'loop_count' in session_events.columns and 'duration_ms' in session_events.columns:
            loop_df = session_events[['phase_name', 'duration_ms', 'loop_count']].copy()
            
            loop_df['Frequency (Hz)'] = loop_df.apply(
                lambda row: (row['loop_count'] / (row['duration_ms'] / 1000.0)) if row['duration_ms'] > 0 else 0,
                axis=1
            )
            loop_df['Avg ms/loop'] = loop_df.apply(
                lambda row: (row['duration_ms'] / row['loop_count']) if row['loop_count'] > 0 else 0,
                axis=1
            )
            
            loop_df.rename(columns={
                'phase_name': 'Phase Name',
                'duration_ms': 'Duration (ms)',
                'loop_count': 'Loop Count'
            }, inplace=True)
            
            st.dataframe(loop_df.style.format({
                'Frequency (Hz)': '{:.1f}',
                'Avg ms/loop': '{:.2f}'
            }))
        else:
            st.info("The 'loop_count' column is not available in the events data for this session.")


    with st.expander("Show Raw Data for Selected Session"):
        st.subheader("Session Data")
        st.dataframe(session_data.to_frame().T)
        st.subheader(f"Events ({len(session_events)} records)")
        st.dataframe(session_events)
        st.subheader(f"Measurements ({len(session_measurements)} records)")
        st.dataframe(session_measurements)

else: # Multi-Session Analysis
    st.sidebar.header("Session Filters")
    profile_options = ["All"] + list(sessions_df['profile_name'].dropna().unique())
    selected_profile = st.sidebar.selectbox("Filter by Profile", profile_options,
                                           help="Filter sessions by grind profile: SINGLE (18g), DOUBLE (36g), CUSTOM (user-defined). Profile ID stored in grind_sessions table.")
    
    if selected_profile != "All":
        filtered_sessions = sessions_df[sessions_df['profile_name'] == selected_profile]
    else:
        filtered_sessions = sessions_df

    min_id, max_id = int(filtered_sessions['session_id'].min()), int(filtered_sessions['session_id'].max())
    selected_ids = st.sidebar.slider("Select Session ID Range", min_id, max_id, (min_id, max_id),
                                     help="Select range of session IDs for analysis. Sessions are auto-incrementing integers stored in flash memory. Newer sessions have higher IDs.")
    
    analysis_df = filtered_sessions[
        (filtered_sessions['session_id'] >= selected_ids[0]) &
        (filtered_sessions['session_id'] <= selected_ids[1])
    ]

    st.header("Multi-Session Performance Analysis")

    if not analysis_df.empty:
        if (analysis_df['mode_name'] == 'WEIGHT').all():
            # Create tabs for different analysis focuses
            tab1, tab2, tab3 = st.tabs(["Grind Session Overview", "Predictive Phase Analysis", "Pulse Phase Analysis"])
            
            # Load details for all selected sessions at once
            multi_session_events, multi_session_measurements = load_all_details()
            multi_session_events = multi_session_events[multi_session_events['session_id'].isin(analysis_df['session_id'])]
            multi_session_measurements = multi_session_measurements[multi_session_measurements['session_id'].isin(analysis_df['session_id'])]

            # Calculate grind time for each session
            grind_times = []
            for session_id in analysis_df['session_id']:
                events = multi_session_events[multi_session_events['session_id'] == session_id]
                predictive_start = events[events['phase_name'] == 'PREDICTIVE']
                if not predictive_start.empty:
                    start_time = predictive_start['timestamp_ms'].min()
                    settling_phases = ['FINAL_SETTLING', 'PULSE_SETTLING']
                    last_settle = events[events['phase_name'].isin(settling_phases)]
                    if not last_settle.empty:
                        end_time = (last_settle['timestamp_ms'] + last_settle['duration_ms']).max()
                        grind_times.append({'session_id': session_id, 'grind_time_s': (end_time - start_time) / 1000.0})

            if grind_times:
                grind_times_df = pd.DataFrame(grind_times)
                analysis_df = pd.merge(analysis_df, grind_times_df, on='session_id', how='left')

            with tab1:
                st.subheader("Overall Grind Performance Metrics")
                
                # Key performance indicators
                col1, col2, col3, col4 = st.columns(4)
                
                # Overall accuracy metrics
                within_tolerance = (abs(analysis_df['error_grams']) < TOLERANCE_G).sum()
                total_sessions = len(analysis_df)
                accuracy_rate = (within_tolerance / total_sessions) * 100 if total_sessions > 0 else 0
                
                col1.metric("Accuracy Rate", f"{accuracy_rate:.1f}%",
                           help=f"Percentage of sessions within ±{TOLERANCE_G}g tolerance. Key metric for algorithm performance.")
                
                avg_error = analysis_df['error_grams'].mean()
                col2.metric("Average Error", f"{avg_error:+.3f}g",
                           help="Mean error across all sessions. Positive = overshoot, negative = undershoot. Should be close to 0.")
                
                std_error = analysis_df['error_grams'].std()
                col3.metric("Error Std Dev", f"{std_error:.3f}g",
                           help="Standard deviation of errors. Lower values indicate more consistent performance.")
                
                avg_grind_time = analysis_df['grind_time_s'].mean() if 'grind_time_s' in analysis_df.columns else 0
                col4.metric("Avg Grind Time", f"{avg_grind_time:.1f}s",
                           help="Average total grind time from predictive start to final settle.")

                # Performance distribution charts
                col1, col2 = st.columns(2)
                
                with col1:
                    st.subheader("Error Distribution")
                    fig_hist = go.Figure(data=[go.Histogram(x=analysis_df['error_grams'], nbinsx=20, name="Error Distribution")])
                    fig_hist.add_vline(x=TOLERANCE_G, line_dash="dash", line_color="red", annotation_text=f"+{TOLERANCE_G}g tolerance")
                    fig_hist.add_vline(x=-TOLERANCE_G, line_dash="dash", line_color="red", annotation_text=f"-{TOLERANCE_G}g tolerance")
                    fig_hist.add_vline(x=0, line_dash="solid", line_color="green", annotation_text="Perfect")
                    fig_hist.update_layout(title="Weight Error Distribution", xaxis_title="Error (g)", yaxis_title="Count")
                    st.plotly_chart(fig_hist, use_container_width=True)
                
                with col2:
                    st.subheader("Result Status Breakdown")
                    result_counts = analysis_df['result_status'].value_counts()
                    fig_pie = go.Figure(data=[go.Pie(labels=result_counts.index, values=result_counts.values)])
                    fig_pie.update_layout(title="Grind Outcomes")
                    st.plotly_chart(fig_pie, use_container_width=True)

                # Performance over time
                st.subheader("Performance Trends")
                fig_trend = go.Figure()
                fig_trend.add_trace(go.Scatter(x=analysis_df['session_id'], y=analysis_df['error_grams'], 
                                             mode='markers', name='Error', marker=dict(size=6, opacity=0.7)))
                fig_trend.add_hline(y=TOLERANCE_G, line_dash="dash", line_color="red", annotation_text=f"+{TOLERANCE_G}g")
                fig_trend.add_hline(y=-TOLERANCE_G, line_dash="dash", line_color="red", annotation_text=f"-{TOLERANCE_G}g")
                fig_trend.add_hline(y=0, line_dash="solid", line_color="green", annotation_text="Perfect")
                fig_trend.update_layout(title="Error vs Session ID (Time Progression)", 
                                      xaxis_title="Session ID", yaxis_title="Error (g)")
                st.plotly_chart(fig_trend, use_container_width=True)
                
            with tab2:
                st.subheader("Predictive Phase Algorithm Tuning")
                
                # Get predictive events for analysis
                predictive_events = multi_session_events[multi_session_events['phase_name'] == 'PREDICTIVE'].copy()
                
                if not predictive_events.empty:
                    # Merge with session results for analysis
                    predictive_analysis = pd.merge(predictive_events, analysis_df[['session_id', 'target_weight', 'final_weight', 'error_grams', 'result_status']], 
                                                 on='session_id', how='left')
                    
                    # Calculate predictive phase accuracy (including coasting)
                    # predictive_yield should be the weight after first settling phase, not just predictive end
                    predictive_analysis['predictive_total_yield'] = predictive_analysis['end_weight'] - predictive_analysis['start_weight']  # This will be updated below
                    predictive_analysis['target_predictive_yield'] = predictive_analysis['target_weight']  # Target is the final weight goal
                    predictive_analysis['predictive_error'] = predictive_analysis['predictive_total_yield'] - predictive_analysis['target_predictive_yield']
                    
                    # Calculate coasting yield and correct predictive total yield
                    coasting_data = []
                    for session_id in predictive_analysis['session_id']:
                        pred_event = predictive_analysis[predictive_analysis['session_id'] == session_id].iloc[0]
                        pred_end_time = pred_event['timestamp_ms'] + pred_event['duration_ms']
                        
                        # Find the first settling phase after predictive end
                        settling_events = multi_session_events[
                            (multi_session_events['session_id'] == session_id) &
                            (multi_session_events['phase_name'].isin(['PULSE_SETTLING', 'FINAL_SETTLING'])) &
                            (multi_session_events['timestamp_ms'] >= pred_end_time)
                        ].sort_values('timestamp_ms')
                        
                        if not settling_events.empty:
                            first_settle = settling_events.iloc[0]
                            coasting_yield = first_settle['end_weight'] - pred_event['end_weight']
                            # Total yield includes coasting
                            predictive_total_yield = first_settle['end_weight'] - pred_event['start_weight']
                            coasting_data.append({
                                'session_id': session_id, 
                                'coasting_yield': coasting_yield,
                                'predictive_total_yield_corrected': predictive_total_yield
                            })
                        else:
                            # No settling phase found, use just predictive phase
                            predictive_total_yield = pred_event['end_weight'] - pred_event['start_weight']
                            coasting_data.append({
                                'session_id': session_id, 
                                'coasting_yield': 0,
                                'predictive_total_yield_corrected': predictive_total_yield
                            })
                    
                    if coasting_data:
                        coasting_df = pd.DataFrame(coasting_data)
                        predictive_analysis = pd.merge(predictive_analysis, coasting_df, on='session_id', how='left')
                        predictive_analysis['coasting_yield'].fillna(0, inplace=True)
                        # Update the predictive total yield to include coasting
                        predictive_analysis['predictive_total_yield'] = predictive_analysis['predictive_total_yield_corrected']
                        # Recalculate error with corrected total yield
                        predictive_analysis['predictive_error'] = predictive_analysis['predictive_total_yield'] - predictive_analysis['target_predictive_yield']
                    
                    # Key predictive metrics
                    col1, col2, col3, col4, col5 = st.columns(5)
                    
                    avg_undershoot = predictive_analysis['motor_stop_target_weight'].mean()
                    col1.metric("Avg Undershoot Target", f"{avg_undershoot:.3f}g",
                               help="Average motor stop timing offset. This controls when the motor stops during predictive phase, not the target yield. Used to account for coasting behavior.")
                    
                    avg_predictive_error = predictive_analysis['predictive_error'].mean()
                    col2.metric("Avg Predictive Error", f"{avg_predictive_error:+.3f}g",
                               help="How close the predictive phase + coasting gets to the final target weight. Negative = undershoot (needs pulses), Positive = overshoot (rare). Measures total predictive effectiveness.")
                    
                    avg_coasting_yield = predictive_analysis['coasting_yield'].mean() if 'coasting_yield' in predictive_analysis.columns else 0
                    col3.metric("Avg Coasting Yield", f"{avg_coasting_yield:.3f}g",
                               help="Average weight gained during settling phase after motor stops. Shows effectiveness of predictive stopping algorithm.")
                    
                    avg_grind_latency = predictive_analysis['grind_latency_ms'].mean()
                    col4.metric("Avg Grind Latency", f"{avg_grind_latency:.0f}ms",
                               help="Average startup time from motor on to flow detection.")
                    
                    avg_pulse_flow_rate = predictive_analysis['pulse_flow_rate'].mean() if 'pulse_flow_rate' in predictive_analysis.columns else 0
                    col5.metric("Avg Flow Rate", f"{avg_pulse_flow_rate:.3f}g/s",
                               help="Average 95th percentile flow rate calculated during predictive phase.")

                    # Analysis charts
                    col1, col2, col3 = st.columns(3)
                    
                    with col1:
                        st.subheader("Undershoot Target Distribution")
                        fig_undershoot = go.Figure(data=[go.Histogram(x=predictive_analysis['motor_stop_target_weight'], 
                                                                     nbinsx=20, name="Undershoot Target")])
                        fig_undershoot.update_layout(title="Motor Stop Target Weight Distribution", 
                                                   xaxis_title="Undershoot Target (g)", yaxis_title="Count")
                        st.plotly_chart(fig_undershoot, use_container_width=True)
                    
                    with col2:
                        st.subheader("Coasting Yield Distribution")
                        if 'coasting_yield' in predictive_analysis.columns:
                            fig_coasting = go.Figure(data=[go.Histogram(x=predictive_analysis['coasting_yield'], 
                                                                       nbinsx=20, name="Coasting Yield")])
                            # Add vertical line for average
                            avg_coasting = predictive_analysis['coasting_yield'].mean()
                            fig_coasting.add_vline(x=avg_coasting, line_dash="dash", line_color="red", 
                                                 annotation_text=f"Avg: {avg_coasting:.3f}g")
                            fig_coasting.update_layout(title="Weight Gained During Coasting", 
                                                     xaxis_title="Coasting Yield (g)", yaxis_title="Count")
                            st.plotly_chart(fig_coasting, use_container_width=True)
                        else:
                            st.info("Coasting yield data not available")
                    
                    with col3:
                        st.subheader("Predictive vs Final Error")
                        fig_pred_error = go.Figure()
                        fig_pred_error.add_trace(go.Scatter(x=predictive_analysis['predictive_error'], 
                                                           y=predictive_analysis['error_grams'],
                                                           mode='markers', name='Sessions',
                                                           customdata=predictive_analysis['session_id'],
                                                           hovertemplate="Session %{customdata}<br>Predictive Error: %{x:.3f}g<br>Final Error: %{y:.3f}g<extra></extra>"))
                        fig_pred_error.add_trace(go.Scatter(x=[-1, 1], y=[-1, 1], mode='lines', 
                                                           line=dict(dash='dash', color='grey'), name='1:1 Line'))
                        fig_pred_error.update_layout(title="Predictive Phase Error vs Final Error",
                                                   xaxis_title="Predictive Error (g)", yaxis_title="Final Error (g)")
                        st.plotly_chart(fig_pred_error, use_container_width=True)

                    # Grind latency analysis
                    st.subheader("Grind Latency vs Performance")
                    fig_latency = go.Figure()
                    fig_latency.add_trace(go.Scatter(x=predictive_analysis['grind_latency_ms'], 
                                                   y=predictive_analysis['error_grams'],
                                                   mode='markers', name='Sessions',
                                                   customdata=predictive_analysis['session_id'],
                                                   hovertemplate="Session %{customdata}<br>Latency: %{x}ms<br>Final Error: %{y:.3f}g<extra></extra>"))
                    fig_latency.update_layout(title="Grind Latency Impact on Final Accuracy",
                                            xaxis_title="Grind Latency (ms)", yaxis_title="Final Error (g)")
                    st.plotly_chart(fig_latency, use_container_width=True)
                    
                else:
                    st.info("No predictive phase data available for the selected sessions.")

            with tab3:
                st.subheader("Pulse Effectiveness Analysis")
                st.markdown("""
                This analysis shows the relationship between pulse duration and coffee weight added across multiple sessions.
                Each point represents a single PULSE_EXECUTE event. The system calculates pulse duration using: 
                `duration_ms = (error_grams / pulse_flow_rate) * 1000`, where pulse_flow_rate comes from the 95th percentile 
                flow rate calculated during the PREDICTIVE phase.
                
                **Correlation Analysis**: Compare how well different flow rate calculation methods predict actual pulse yields.
                Higher correlation (r closer to 1.0) indicates better predictive accuracy for pulse duration calculations.
                """)
                
                # --- REFACTORED Pulse Summary Calculation ---
                # This vectorized approach using `merge_asof` is significantly faster
                # than iterating through sessions and events in Python loops.
                pulse_executes = multi_session_events[multi_session_events['phase_name'] == 'PULSE_EXECUTE'].copy()
                pulse_settles = multi_session_events[multi_session_events['phase_name'] == 'PULSE_SETTLING'][['session_id', 'timestamp_ms', 'end_weight']].copy()

                if not pulse_executes.empty:
                    pulse_settles.rename(columns={'end_weight': 'settle_end_weight'}, inplace=True)
                    pulse_executes.sort_values('timestamp_ms', inplace=True)
                    pulse_settles.sort_values('timestamp_ms', inplace=True)

                    # Get the pulse flow rate from each session's predictive event
                    predictive_events = multi_session_events[multi_session_events['phase_name'] == 'PREDICTIVE'].copy()
                
                    # Create a dictionary for fast lookup of pulse_flow_rate by session_id
                    pulse_flow_rates = {}
                    if 'pulse_flow_rate' in predictive_events.columns:
                        for _, event in predictive_events.iterrows():
                            pulse_flow_rates[event['session_id']] = event['pulse_flow_rate']
                
                    # Calculate average flow rate over last 1500ms of predictive phase for each session
                    avg_flow_rates_1500ms = {}
                    for session_id in analysis_df['session_id']:
                        # Get predictive event for this session
                        pred_event = predictive_events[predictive_events['session_id'] == session_id]
                        if not pred_event.empty:
                            pred_start = pred_event['timestamp_ms'].iloc[0]
                            pred_end = pred_start + pred_event['duration_ms'].iloc[0]
                        
                            # Get measurements for last 1500ms of predictive phase
                            window_start = pred_end - 1500  # Last 1500ms
                            session_measurements = multi_session_measurements[
                                (multi_session_measurements['session_id'] == session_id) &
                                (multi_session_measurements['timestamp_ms'] >= window_start) &
                                (multi_session_measurements['timestamp_ms'] <= pred_end) &
                                (multi_session_measurements['phase_name'] == 'PREDICTIVE')
                            ]
                            
                            if not session_measurements.empty:
                                avg_flow_rate = session_measurements['flow_rate_g_per_s'].mean()
                                avg_flow_rates_1500ms[session_id] = avg_flow_rate
                            else:
                                avg_flow_rates_1500ms[session_id] = 0
                        else:
                            avg_flow_rates_1500ms[session_id] = 0

                    merged_pulses = pd.merge_asof(
                        pulse_executes, pulse_settles,
                        on='timestamp_ms', by='session_id', direction='forward',
                        tolerance=5000 # Use integer tolerance (5000ms) to match int64 timestamp
                    )
                
                    # Add pulse_flow_rate using dictionary lookup
                    merged_pulses['pulse_flow_rate'] = merged_pulses['session_id'].map(pulse_flow_rates).fillna(0)
                
                    # Add 1500ms average flow rate using dictionary lookup
                    merged_pulses['avg_flow_rate_1500ms'] = merged_pulses['session_id'].map(avg_flow_rates_1500ms).fillna(0)

                    merged_pulses['final_pulse_weight'] = merged_pulses['settle_end_weight'].fillna(merged_pulses['end_weight'])
                    merged_pulses['pulse_yield'] = (merged_pulses['final_pulse_weight'] - merged_pulses['start_weight']).clip(lower=0)
                    
                    # Calculate expected yield using original pulse_flow_rate (95th percentile method)
                    merged_pulses['expected_yield'] = (merged_pulses['pulse_duration_ms'] / 1000.0) * merged_pulses['pulse_flow_rate']
                    
                    # Calculate expected yield using 1500ms average flow rate
                    merged_pulses['expected_yield_1500ms'] = (merged_pulses['pulse_duration_ms'] / 1000.0) * merged_pulses['avg_flow_rate_1500ms']

                    pulse_summary_df = merged_pulses.rename(columns={
                        'session_id': 'Session ID',
                        'pulse_duration_ms': 'Duration (ms)',
                        'pulse_yield': 'Pulse Yield (g)'
                    })

                    fig_pulse_eff = go.Figure(data=go.Scatter(
                        x=pulse_summary_df['Duration (ms)'], 
                        y=pulse_summary_df['Pulse Yield (g)'],
                        mode='markers', marker=dict(size=8, opacity=0.6),
                        customdata=pulse_summary_df['Session ID'],
                        hovertemplate="<b>Session %{customdata}</b><br>Duration: %{x} ms<br>Pulse Yield: %{y:.3f}g<extra></extra>"
                    ))
                    fig_pulse_eff.update_layout(title="Pulse Duration vs. Weight Added (All Sessions)",
                                                xaxis_title="Pulse Duration (ms)", yaxis_title="Weight Added (g)")
                    
                    # Calculate correlation coefficients
                    correlation_95p = pulse_summary_df[['expected_yield', 'Pulse Yield (g)']].corr().iloc[0, 1] if not pulse_summary_df[['expected_yield', 'Pulse Yield (g)']].isna().any().any() else np.nan
                    correlation_1500ms = pulse_summary_df[['expected_yield_1500ms', 'Pulse Yield (g)']].corr().iloc[0, 1] if not pulse_summary_df[['expected_yield_1500ms', 'Pulse Yield (g)']].isna().any().any() else np.nan

                    # --- Expected vs Actual Chart (95th Percentile Method) ---
                    fig_accuracy_95p = go.Figure()
                    fig_accuracy_95p.add_trace(go.Scatter(
                        x=pulse_summary_df['expected_yield'], y=pulse_summary_df['Pulse Yield (g)'],
                        mode='markers', marker=dict(size=8, opacity=0.6),
                        customdata=pulse_summary_df['Session ID'],
                        hovertemplate="<b>Session %{customdata}</b><br>Expected: %{x:.3f}g<br>Actual: %{y:.3f}g<extra></extra>",
                        name='Pulses'
                    ))
                    # Add a 1:1 line for reference
                    min_val = min(pulse_summary_df['expected_yield'].min(), pulse_summary_df['Pulse Yield (g)'].min())
                    max_val = max(pulse_summary_df['expected_yield'].max(), pulse_summary_df['Pulse Yield (g)'].max())
                    fig_accuracy_95p.add_trace(go.Scatter(
                        x=[min_val, max_val], y=[min_val, max_val],
                        mode='lines', line=dict(dash='dash', color='grey'), name='Perfect Prediction'
                    ))
                    fig_accuracy_95p.update_layout(
                        title_text=f'Expected vs. Actual (95th Percentile)<br><sub>Correlation: r = {correlation_95p:.3f}</sub>', 
                        xaxis_title="Expected Yield (g)", yaxis_title="Actual Pulse Yield (g)"
                    )

                    # --- Expected vs Actual Chart (1500ms Average Method) ---
                    fig_accuracy_1500ms = go.Figure()
                    fig_accuracy_1500ms.add_trace(go.Scatter(
                        x=pulse_summary_df['expected_yield_1500ms'], y=pulse_summary_df['Pulse Yield (g)'],
                        mode='markers', marker=dict(size=8, opacity=0.6, color='orange'),
                        customdata=pulse_summary_df['Session ID'],
                        hovertemplate="<b>Session %{customdata}</b><br>Expected: %{x:.3f}g<br>Actual: %{y:.3f}g<extra></extra>",
                        name='Pulses'
                    ))
                    # Add a 1:1 line for reference
                    min_val_1500 = min(pulse_summary_df['expected_yield_1500ms'].min(), pulse_summary_df['Pulse Yield (g)'].min())
                    max_val_1500 = max(pulse_summary_df['expected_yield_1500ms'].max(), pulse_summary_df['Pulse Yield (g)'].max())
                    fig_accuracy_1500ms.add_trace(go.Scatter(
                        x=[min_val_1500, max_val_1500], y=[min_val_1500, max_val_1500],
                        mode='lines', line=dict(dash='dash', color='grey'), name='Perfect Prediction'
                    ))
                    fig_accuracy_1500ms.update_layout(
                        title_text=f'Expected vs. Actual (1500ms Average)<br><sub>Correlation: r = {correlation_1500ms:.3f}</sub>', 
                        xaxis_title="Expected Yield (g)", yaxis_title="Actual Pulse Yield (g)"
                    )

                    # Display charts in columns
                    col1, col2, col3 = st.columns(3)
                    
                    with col1:
                        st.plotly_chart(fig_pulse_eff, use_container_width=True)
                    
                    with col2:
                        st.metric("95th Percentile Method Correlation", 
                                 f"r = {correlation_95p:.3f}",
                                 help="Pearson correlation coefficient (r) measures how well the 95th percentile flow rate from the predictive phase predicts actual pulse yields. Values: 1.0 = perfect, 0.8-1.0 = strong, 0.6-0.8 = moderate, 0.4-0.6 = weak, 0.0 = no correlation")
                        st.plotly_chart(fig_accuracy_95p, use_container_width=True)
                    
                    with col3:
                        st.metric("1500ms Average Method Correlation",
                                 f"r = {correlation_1500ms:.3f}",
                                 help="Pearson correlation coefficient (r) measures how well the average flow rate from the last 1500ms of predictive phase predicts actual pulse yields. Higher correlation indicates better predictive accuracy for pulse duration calculations.")
                        st.plotly_chart(fig_accuracy_1500ms, use_container_width=True)
                else:
                    st.info("No pulse data available for the selected sessions.")

            st.subheader("Data from Selected Sessions")
            st.dataframe(analysis_df)
        elif (analysis_df['mode_name'] == 'TIME').all():
            st.info("Multi-session analysis for grind-by-time sessions focuses on time accuracy metrics. Please filter to grind-by-weight sessions to view the full predictive and pulse analytics.")
        else:
            st.warning("Mixed grind modes detected. Please filter to a single mode to view analytics.")
    else:
        st.warning("No sessions match the selected filters.")
