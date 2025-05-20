import csv
import datetime
import random
import time
import json
import paho.mqtt.client as mqtt
import matplotlib.pyplot as plt
import pandas as pd
import numpy as np

# --- Configuration ---
BROKER = "karis.cloud"  # Or your MQTT broker's address
PORT = 1883
DEVICE_ID = "esp32_6relay"  # Change if your device ID is different
TOPIC_CONTROL = f"irrigation/{DEVICE_ID}/control"
TOPIC_STATUS = f"irrigation/{DEVICE_ID}/status"
API_KEY = "8a679613-019f-4b88-9068-da10f09dcdd2"  # Replace with your actual API key

NUM_TESTS = 100  # Total number of test iterations
WAIT_BETWEEN_TESTS = 1  # Wait time between tests (seconds)
RELAY_IDS = [1, 2, 3, 4, 5, 6]  # IDs of the relays to be tested
RESPONSE_TIMEOUT = 5  # Time to wait for a response from the relays (seconds)

# --- Global Variables ---
# These variables are used to share state between MQTT callbacks and the main loop.
latency_records = []  # Stores records of latency and status for each command
start_times = {}  # Tracks the send time of commands to calculate latency {relay_id: timestamp}
expected_states_global = {}  # Stores the expected state for each relay in the current test {relay_id: bool}
waiting_for_response = set()  # Set of relay IDs for which a response is currently awaited
current_test_scenario = ""  # Name of the current test scenario being executed
total_commands_sent_counter = 0  # Counter for all individual relay commands issued
previous_overall_commanded_states = {} # Stores the state of all relays from the PREVIOUS test iteration
command_details_for_record = {} # Stores additional command details like 'command_implies_change' for the current test

# --- Ghi chú về tính khách quan của bài test ---
# 1. Tập lệnh này đo lường độ trễ round-trip từ máy chạy script đến thiết bị và ngược lại, thông qua MQTT broker.
# 2. Kết quả bao gồm tất cả các thành phần trễ: mạng cục bộ, mạng internet (nếu broker ở xa),
#    thời gian xử lý của broker, thời gian truyền đến thiết bị, thời gian xử lý của thiết bị,
#    và đường truyền ngược lại.
# 3. Để đảm bảo kết quả khách quan nhất:
#    a. Môi trường mạng (máy chạy script, broker, thiết bị) cần ổn định.
#    b. Thiết bị (ESP32) cần có thời gian phản hồi nhất quán.
#    c. Máy chạy script không nên bị quá tải bởi các tác vụ khác.
# 4. Tập lệnh này không thể tự loại bỏ các yếu tố biến động từ môi trường mạng hoặc hiệu suất thiết bị/broker.
#    Nó cung cấp một phép đo khách quan về hiệu suất của toàn bộ hệ thống tại thời điểm test.

# --- Utility Functions ---
def get_time_period():
    """Determines the period of the day (morning, afternoon, evening)."""
    hour = datetime.datetime.now().hour
    if 5 <= hour < 12:
        return "morning"
    elif 12 <= hour < 18:
        return "afternoon"
    else:
        return "evening"

def get_relay_states_for_scenario(scenario_name, relay_ids):
    """Generates a dictionary of relay states based on the specified scenario."""
    states = {}
    if scenario_name == "all_on":
        for rid in relay_ids:
            states[rid] = True
    elif scenario_name == "all_off":
        for rid in relay_ids:
            states[rid] = False
    elif scenario_name == "alternate_1":  # True, False, True...
        for i, rid in enumerate(relay_ids):
            states[rid] = (i % 2 == 0)
    elif scenario_name == "alternate_2":  # False, True, False...
        for i, rid in enumerate(relay_ids):
            states[rid] = (i % 2 != 0)
    elif scenario_name == "random":
        for rid in relay_ids:
            states[rid] = bool(random.getrandbits(1))
    else:
        print(f"Warning: Undefined scenario '{scenario_name}', defaulting to random states.")
        for rid in relay_ids:
            states[rid] = bool(random.getrandbits(1))
    return states

# --- MQTT Callbacks ---
def on_connect(client, userdata, flags, rc):
    """Callback executed when the client successfully connects to the MQTT broker."""
    if rc == 0:
        print(f"Successfully connected to MQTT Broker: {BROKER}")
        client.subscribe(TOPIC_STATUS)  # Subscribe to the status topic
        print(f"Listening on topic: {TOPIC_STATUS}")
    else:
        print(f"MQTT connection failed, error code: {rc}")

def on_message(client, userdata, msg):
    """
    Callback executed when a message is received from the MQTT broker.
    This function processes incoming status messages from relays.
    """
    global waiting_for_response, latency_records, start_times, expected_states_global, current_test_scenario, command_details_for_record

    try:
        payload_str = msg.payload.decode()
        payload = json.loads(payload_str)
        timestamp_received = datetime.datetime.now() # Timestamp of message reception by the script

        for relay_status in payload.get("relays", []):
            rid = relay_status.get("id")
            r_state = relay_status.get("state")

            # Critical check: Is this a response we are actively waiting for and does it match expectations?
            if rid in waiting_for_response and rid in expected_states_global and r_state == expected_states_global[rid]:
                if rid in start_times: # Ensure there's a start time recorded for this relay ID
                    # Calculate latency
                    latency = timestamp_received.timestamp() - start_times[rid]
                    latency_ms = latency * 1000

                    current_command_details = command_details_for_record.get(rid, {})
                    record = {
                        "timestamp": timestamp_received.isoformat(),
                        "period": get_time_period(),
                        "relay_id": rid,
                        "state_commanded": expected_states_global[rid],
                        "state_received": r_state,
                        "latency_ms": round(latency_ms, 2),
                        "scenario": current_test_scenario,
                        "status": "success",
                        "command_implies_change": current_command_details.get("command_implies_change", None)
                    }
                    latency_records.append(record)
                    
                    # Clean up: remove from sets/dicts for relays that have responded
                    waiting_for_response.remove(rid)
                    del start_times[rid]
                # else: If rid is not in start_times, it implies an issue or a late message
                # already handled by timeout. The script correctly ignores it for latency calculation.
            # else: If not waiting for this rid, or state doesn't match, or not in expected_states,
            # it could be an unsolicited message, a duplicate, or a message for a previous test.
            # The script correctly ignores these for the current test's latency record.
    except json.JSONDecodeError:
        print(f"Error decoding JSON from payload: {msg.payload.decode()}")
    except Exception as e:
        print(f"Error in on_message: {e}")

# --- MQTT Client Setup ---
client = mqtt.Client(client_id=f"latency_tester_{random.randint(1000,9999)}")
client.on_connect = on_connect
client.on_message = on_message

try:
    print(f"Connecting to MQTT Broker: {BROKER}...")
    client.connect(BROKER, PORT, 60)  # 60-second keepalive
except Exception as e:
    print(f"Could not connect to MQTT Broker: {e}")
    exit()

client.loop_start()  # Start network loop in a separate thread

# --- List of scenarios to run ---
scenarios = ["all_on", "all_off", "alternate_1", "alternate_2", "random"]

# --- Main Test Loop ---
for i in range(NUM_TESTS):
    current_test_scenario = scenarios[i % len(scenarios)]  # Cycle through scenarios
    print(f"\n--- Starting Test {i+1}/{NUM_TESTS} (Scenario: {current_test_scenario}) ---")

    # Generate initial expected states for the selected scenario
    expected_states_global = get_relay_states_for_scenario(current_test_scenario, RELAY_IDS)

    # Try to ensure this test iteration commands a different overall state than the last one
    # This is a best-effort attempt to make tests more dynamic.
    max_scenario_adjust_attempts = 3 # Prevent infinite loops for scenario adjustment
    attempt = 0
    original_scenario_for_log = current_test_scenario
    while expected_states_global == previous_overall_commanded_states and attempt < max_scenario_adjust_attempts:
        attempt += 1
        print(f"  Note: Scenario '{current_test_scenario}' resulted in same overall state as previous test. Adjusting (Attempt {attempt})...")
        if current_test_scenario == "random":
            expected_states_global = get_relay_states_for_scenario("random", RELAY_IDS) # Re-randomize
            if expected_states_global != previous_overall_commanded_states:
                print(f"  Adjusted: 'random' scenario re-randomized.")
                break
        else: # For deterministic scenarios, try cycling to the next one
            scenario_index = scenarios.index(current_test_scenario)
            next_scenario_index = (scenario_index + 1) % len(scenarios)
            current_test_scenario = scenarios[next_scenario_index]
            expected_states_global = get_relay_states_for_scenario(current_test_scenario, RELAY_IDS)
            if expected_states_global != previous_overall_commanded_states:
                print(f"  Adjusted: Switched scenario from '{original_scenario_for_log}' to '{current_test_scenario}'.")
                break
        if attempt == max_scenario_adjust_attempts and expected_states_global == previous_overall_commanded_states:
            print(f"  Warning: Could not achieve a different overall state after {max_scenario_adjust_attempts} attempts for scenario '{original_scenario_for_log}'. Proceeding with current states.")


    relays_payload_list = []
    command_details_for_record.clear() # Clear for the current test

    # Clear state for the new test iteration to ensure no data contamination
    waiting_for_response.clear()
    # start_times is cleared on a per-relay basis upon receiving a response or timeout.

    current_command_time = time.time()  # Timestamp just before preparing and sending the batch command
    for rid in RELAY_IDS:
        total_commands_sent_counter += 1  # Increment for each individual relay operation commanded
        target_state = expected_states_global[rid]
        
        # Determine if this command implies a state change from the *previous test's* commanded state for this relay
        # For the very first test, previous_overall_commanded_states will be empty.
        previous_relay_state = previous_overall_commanded_states.get(rid)
        implies_change = (previous_relay_state is None) or (previous_relay_state != target_state)
        command_details_for_record[rid] = {"command_implies_change": implies_change}

        if not implies_change:
            print(f"  Info: Relay {rid} commanded to '{target_state}', same as previous test's command for this relay.")

        relays_payload_list.append({"id": rid, "state": target_state})
        # All relays in this batch share the same command initiation time from the script's perspective
        start_times[rid] = current_command_time
        waiting_for_response.add(rid)  # Mark this relay as awaiting response

    command_payload = {
        "api_key": API_KEY,
        "relays": relays_payload_list
    }

    # Publish the single command payload for all relays in this test iteration
    # QoS is 0 by default (at most once delivery). Failures to deliver to broker might result in timeouts.
    client.publish(TOPIC_CONTROL, json.dumps(command_payload))
    # print(f"Published command to {TOPIC_CONTROL}: {json.dumps(command_payload)}") # Optional: for debugging

    test_start_time_for_timeout_check = time.time()
    # Wait for responses or timeout for this test iteration
    while waiting_for_response and (time.time() - test_start_time_for_timeout_check) < RESPONSE_TIMEOUT:
        time.sleep(0.1)  # Check frequently but allow other operations

    # Handle any relays that did not respond within the timeout period
    if waiting_for_response: # If set is not empty, some relays timed out
        timestamp_timeout = datetime.datetime.now().isoformat()
        # Iterate over a copy of the set because we might modify start_times
        for rid_timeout in list(waiting_for_response):
            print(f"  Warning: Relay {rid_timeout} DID NOT respond within {RESPONSE_TIMEOUT} seconds.")
            current_command_details_timeout = command_details_for_record.get(rid_timeout, {})
            record = {
                "timestamp": timestamp_timeout,
                "period": get_time_period(),
                "relay_id": rid_timeout,
                "state_commanded": expected_states_global.get(rid_timeout), # Use .get for safety if rid somehow not in expected
                "state_received": None,
                "latency_ms": None, # No latency can be calculated for timeouts
                "scenario": current_test_scenario, # Use the (potentially adjusted) scenario name
                "status": "timeout",
                "command_implies_change": current_command_details_timeout.get("command_implies_change", None)
            }
            latency_records.append(record)
            if rid_timeout in start_times:
                del start_times[rid_timeout] # Clean up start time for timed-out relay
        waiting_for_response.clear() # All pending responses for this test are now handled (as timeouts)

    # Store the commanded states of this iteration for the next iteration's comparison
    previous_overall_commanded_states = expected_states_global.copy()

    print(f"Waiting {WAIT_BETWEEN_TESTS} seconds before starting the next test...")
    time.sleep(WAIT_BETWEEN_TESTS)

print("\n--- All tests completed ---")
client.loop_stop()  # Stop the MQTT network loop
client.disconnect()
print("Disconnected from MQTT Broker.")

# --- Data Processing and Statistics ---
# This section processes the collected 'latency_records' to derive meaningful statistics.
# The logic here is for data analysis and does not affect the objectivity of data collection itself.
df_records = pd.DataFrame(latency_records)
overall_success_rate = 0  # Initialize
total_successful_responses = 0
total_timeouts = 0
df_valid_latency = pd.DataFrame() # Initialize as empty DataFrame for records with actual latency

if not df_records.empty:
    print("\n--- Performance Statistics ---")
    total_successful_responses = df_records[df_records['status'] == 'success'].shape[0]
    total_timeouts = df_records[df_records['status'] == 'timeout'].shape[0]

    # Sanity check: total_commands_sent_counter should ideally equal (total_successful_responses + total_timeouts)
    # which is also len(df_records)
    if total_commands_sent_counter != len(df_records):
        print(f"Warning: Mismatch in command count. Sent: {total_commands_sent_counter}, Recorded: {len(df_records)}")


    print(f"Total Commands Sent to Relays: {total_commands_sent_counter}")
    print(f"Total Successful Responses: {total_successful_responses}")
    print(f"Total Timeouts: {total_timeouts}")


    if total_commands_sent_counter > 0:
        overall_success_rate = (total_successful_responses / total_commands_sent_counter) * 100
        print(f"Overall Success Rate: {overall_success_rate:.2f}%")
    else:
        print("Overall Success Rate: N/A (No commands sent)")

    # Create a DataFrame with only valid latency values for statistical calculations
    df_valid_latency = df_records.dropna(subset=['latency_ms']).copy() # Use .copy() to avoid SettingWithCopyWarning
    if not df_valid_latency.empty:
        print("\nOverall Latency Statistics (for successful responses):")
        print(f"  Average: {df_valid_latency['latency_ms'].mean():.2f} ms")
        print(f"  Median: {df_valid_latency['latency_ms'].median():.2f} ms")
        print(f"  Min: {df_valid_latency['latency_ms'].min():.2f} ms")
        print(f"  Max: {df_valid_latency['latency_ms'].max():.2f} ms")
        print(f"  Std Dev: {df_valid_latency['latency_ms'].std():.2f} ms")
        print(f"  95th Percentile: {df_valid_latency['latency_ms'].quantile(0.95):.2f} ms")
    else:
        print("\nOverall Latency Statistics: No successful responses with latency data.")

    print("\n--- Per Relay Statistics ---")
    # per_relay_summary_for_txt_report = [] # This list was for console output, text report recalculates
    for relay_id in RELAY_IDS:
        df_relay_specific = df_records[df_records['relay_id'] == relay_id]
        if not df_relay_specific.empty:
            relay_total_commands = df_relay_specific.shape[0] # Total attempts for this relay
            relay_successes = df_relay_specific[df_relay_specific['status'] == 'success'].shape[0]
            relay_success_rate = (relay_successes / relay_total_commands) * 100 if relay_total_commands > 0 else 0

            df_relay_latency = df_relay_specific.dropna(subset=['latency_ms'])
            avg_relay_latency_ms_val = df_relay_latency['latency_ms'].mean() if not df_relay_latency.empty else None
            median_relay_latency_ms_val = df_relay_latency['latency_ms'].median() if not df_relay_latency.empty else None
            
            avg_relay_latency_ms_str = f"{avg_relay_latency_ms_val:.2f} ms" if avg_relay_latency_ms_val is not None else 'N/A'
            median_relay_latency_ms_str = f"{median_relay_latency_ms_val:.2f} ms" if median_relay_latency_ms_val is not None else 'N/A'

            relay_stat_str = f"Relay ID: {relay_id}\n" \
                             f"  Commands: {relay_total_commands}, Successes: {relay_successes}, Success Rate: {relay_success_rate:.2f}%\n"
            if not df_relay_latency.empty:
                relay_stat_str += f"  Latency Avg: {avg_relay_latency_ms_str}, Median: {median_relay_latency_ms_str}\n"
                relay_stat_str += f"  Latency Min: {df_relay_latency['latency_ms'].min():.2f} ms, Max: {df_relay_latency['latency_ms'].max():.2f} ms"
            else:
                relay_stat_str += "  Latency: No successful responses with latency data for this relay."
            print(relay_stat_str)
            # per_relay_summary_for_txt_report.append(relay_stat_str) # Not strictly needed as text report recalculates
        else:
            no_data_str = f"\nRelay ID: {relay_id} - No data recorded for this relay." # Should not happen if RELAY_IDS is used consistently
            print(no_data_str)
            # per_relay_summary_for_txt_report.append(no_data_str)

    print("\n--- Per Scenario Statistics ---")
    for scenario_name in scenarios: # Use the defined scenarios list
        df_scenario_specific = df_records[df_records['scenario'] == scenario_name]
        if not df_scenario_specific.empty:
            scenario_total_commands = df_scenario_specific.shape[0]
            scenario_successes = df_scenario_specific[df_scenario_specific['status'] == 'success'].shape[0]
            scenario_success_rate = (scenario_successes / scenario_total_commands) * 100 if scenario_total_commands > 0 else 0
            print(f"\nScenario: {scenario_name}")
            print(f"  Commands: {scenario_total_commands}, Successes: {scenario_successes}, Success Rate: {scenario_success_rate:.2f}%")

            df_scenario_latency = df_scenario_specific.dropna(subset=['latency_ms'])
            if not df_scenario_latency.empty:
                print(f"  Latency Avg: {df_scenario_latency['latency_ms'].mean():.2f} ms, Median: {df_scenario_latency['latency_ms'].median():.2f} ms")
                print(f"  Latency Min: {df_scenario_latency['latency_ms'].min():.2f} ms, Max: {df_scenario_latency['latency_ms'].max():.2f} ms")
            else:
                print("  Latency: No successful responses with latency data for this scenario.")
        else:
            print(f"\nScenario: {scenario_name} - No data recorded for this scenario.") # Expected if NUM_TESTS is small relative to num scenarios

else: # df_records is empty
    print("Warning: No data recorded during the tests. Cannot generate statistics.")


# --- Save results to CSV ---
if not df_records.empty:
    # Add timestamp to filename to prevent overwriting and for easier tracking
    csv_filename = f"latency_log_{DEVICE_ID}_{datetime.datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    print(f"\nSaving results to CSV file: {csv_filename}")
    # Define fieldnames to ensure consistent column order in CSV
    fieldnames = ["timestamp", "period", "scenario", "relay_id", "state_commanded", "state_received", "latency_ms", "status", "command_implies_change"]
    # Reindex DataFrame to match fieldnames order; missing columns will be NaN, extra columns ignored.
    df_records_to_save = df_records.reindex(columns=fieldnames)
    df_records_to_save.to_csv(csv_filename, index=False, encoding='utf-8-sig') # utf-8-sig for Excel compatibility
    print(f"CSV file saved: {csv_filename}")
else:
    print("Warning: No latency records to save to CSV.")

# --- Plotting Functions ---
# These functions visualize the collected data. Their logic is for presentation
# and does not influence the objectivity of the data collection phase.
def plot_latency_over_time(valid_latency_df, relay_ids_list, device_id_str):
    """Plots latency over time for each relay."""
    if valid_latency_df.empty:
        print("Plotting Info: No valid latency data to plot 'Latency over Time'.")
        return
    plt.figure(figsize=(12, 6))
    plt.title(f'Latency over Time of Recording ({device_id_str})')
    # Convert timestamp strings to datetime objects for plotting if not already
    if 'timestamp_dt' not in valid_latency_df.columns: # Make idempotent
        valid_latency_df.loc[:, 'timestamp_dt'] = pd.to_datetime(valid_latency_df['timestamp'])

    for relay_id_plot in relay_ids_list:
        df_relay = valid_latency_df[valid_latency_df['relay_id'] == relay_id_plot]
        if not df_relay.empty:
            plt.plot(df_relay['timestamp_dt'], df_relay['latency_ms'], marker='o', linestyle='-', markersize=4, label=f'Relay {relay_id_plot}')
    plt.xlabel('Time')
    plt.ylabel('Latency (ms)')
    plt.legend(fontsize='small')
    plt.xticks(rotation=30)
    plt.tight_layout()
    plt.savefig(f"report_latency_over_time_{device_id_str}.png")
    print(f"Saved plot: report_latency_over_time_{device_id_str}.png")
    plt.close()

def plot_average_latency_per_relay(valid_latency_df, device_id_str):
    """Plots average latency for each relay as a bar chart."""
    if valid_latency_df.empty:
        print("Plotting Info: No valid latency data to plot 'Average Latency per Relay'.")
        return
    avg_latency_per_relay = valid_latency_df.groupby('relay_id')['latency_ms'].mean()
    if not avg_latency_per_relay.empty:
        plt.figure(figsize=(8, 6))
        bars = plt.bar(avg_latency_per_relay.index.astype(str), avg_latency_per_relay.values, color='skyblue')
        plt.title(f'Average Latency per Relay ({device_id_str})')
        plt.xlabel('Relay ID')
        plt.ylabel('Average Latency (ms)')
        for bar_item in bars:
            yval = bar_item.get_height()
            plt.text(bar_item.get_x() + bar_item.get_width()/2.0, yval + max(0.5, yval*0.02), f"{yval:.2f}", ha='center', va='bottom')
        plt.tight_layout()
        plt.savefig(f"report_avg_latency_per_relay_{device_id_str}.png")
        print(f"Saved plot: report_avg_latency_per_relay_{device_id_str}.png")
        plt.close()
    else:
        print("Plotting Info: No data to plot for 'Average Latency per Relay' after grouping.")


def plot_latency_distribution(valid_latency_df, device_id_str):
    """Plots a histogram of latency distribution."""
    if valid_latency_df.empty:
        print("Plotting Info: No valid latency data to plot 'Latency Distribution'.")
        return
    plt.figure(figsize=(8, 6))
    plt.hist(valid_latency_df['latency_ms'], bins=30, color='lightcoral', edgecolor='black')
    plt.title(f'Latency Distribution ({device_id_str})')
    plt.xlabel('Latency (ms)')
    plt.ylabel('Frequency')
    plt.tight_layout()
    plt.savefig(f"report_latency_distribution_{device_id_str}.png")
    print(f"Saved plot: report_latency_distribution_{device_id_str}.png")
    plt.close()

def plot_latency_by_scenario(valid_latency_df, device_id_str):
    """Plots boxplots of latency grouped by scenario."""
    if valid_latency_df.empty:
        print("Plotting Info: No valid latency data to plot 'Latency by Scenario'.")
        return
    df_plot = valid_latency_df.copy()
    scenario_order = sorted(df_plot['scenario'].unique())
    df_plot.loc[:, 'scenario_cat'] = pd.Categorical(df_plot['scenario'], categories=scenario_order, ordered=True)

    if not df_plot.empty: # Should always be true if valid_latency_df was not empty
        plt.figure(figsize=(10, 6))
        df_plot.boxplot(column='latency_ms', by='scenario_cat', grid=True, notch=True, patch_artist=True)
        plt.title(f'Latency by Scenario ({device_id_str})')
        plt.suptitle('')
        plt.xlabel('Scenario')
        plt.ylabel('Latency (ms)')
        plt.xticks(rotation=15)
        plt.tight_layout()
        plt.savefig(f"report_latency_by_scenario_{device_id_str}.png")
        print(f"Saved plot: report_latency_by_scenario_{device_id_str}.png")
        plt.close()
    # else: # This case should ideally not be reached if initial check passed.
    #     print("Plotting Info: No data to plot for 'Latency by Scenario' after processing.")

def plot_success_rate_per_relay(all_records_df, relay_ids_list, device_id_str):
    """Plots success rate for each relay as a bar chart."""
    if all_records_df.empty:
        print("Plotting Info: No records data to plot 'Success Rate per Relay'.")
        return
    relay_success_rates = {}
    for r_id in relay_ids_list:
        df_r = all_records_df[all_records_df['relay_id'] == r_id]
        if not df_r.empty:
            relay_success_rates[str(r_id)] = (df_r[df_r['status'] == 'success'].shape[0] / df_r.shape[0]) * 100
        else:
            relay_success_rates[str(r_id)] = 0

    if relay_success_rates: # Check if dictionary is not empty
        plt.figure(figsize=(8, 6))
        bars = plt.bar(relay_success_rates.keys(), relay_success_rates.values(), color='lightgreen')
        plt.title(f'Success Rate per Relay ({device_id_str})')
        plt.xlabel('Relay ID')
        plt.ylabel('Success Rate (%)')
        plt.ylim(0, 105)
        for bar_item in bars:
            yval = bar_item.get_height()
            plt.text(bar_item.get_x() + bar_item.get_width()/2.0, yval + 1, f"{yval:.1f}%", ha='center', va='bottom')
        plt.tight_layout()
        plt.savefig(f"report_success_rate_per_relay_{device_id_str}.png")
        print(f"Saved plot: report_success_rate_per_relay_{device_id_str}.png")
        plt.close()
    else:
        print("Plotting Info: No success rate data to plot for relays.")

def plot_command_change_vs_no_change_latency(valid_latency_df, device_id_str):
    """Plots boxplots of latency grouped by whether the command implied a state change."""
    if valid_latency_df.empty or 'command_implies_change' not in valid_latency_df.columns:
        print("Plotting Info: No valid data or 'command_implies_change' column to plot 'Latency by Command Type'.")
        return
    
    df_plot = valid_latency_df.dropna(subset=['command_implies_change']) # Ensure no NaN in this column
    if df_plot['command_implies_change'].nunique() < 2:
        print("Plotting Info: Not enough variation in 'command_implies_change' to create a meaningful boxplot (e.g., all True or all False).")
        return

    plt.figure(figsize=(8, 6))
    # Ensure boolean values for 'by' parameter in boxplot
    df_plot.loc[:, 'command_implies_change_bool'] = df_plot['command_implies_change'].astype(bool)
    df_plot.boxplot(column='latency_ms', by='command_implies_change_bool', grid=True, notch=True, patch_artist=True)
    plt.title(f'Latency: State Change Commanded vs. No Change ({device_id_str})')
    plt.suptitle('') # Remove default suptitle
    plt.xlabel('Command Implied State Change')
    plt.ylabel('Latency (ms)')
    # Ensure xticks are set meaningfully if 'by' creates categorical ticks
    # The actual tick labels might depend on how pandas' boxplot handles the boolean `by` column
    # For clarity, explicitly setting them might be useful if default is not "True" / "False"
    # ax = plt.gca()
    # ax.set_xticklabels(['No Change Commanded', 'Change Commanded']) # Example if default is 0, 1
    plt.tight_layout()
    plt.savefig(f"report_latency_by_command_type_{device_id_str}.png")
    print(f"Saved plot: report_latency_by_command_type_{device_id_str}.png")
    plt.close()

def save_text_summary_as_image(total_cmds, total_succ, total_timeo, overall_rate, valid_lat_df, device_id_str):
    """Saves a text summary of overall performance as a PNG image."""
    plt.figure(figsize=(8, 6)) # Adjust figure size as needed
    plt.axis('off')
    stats_text = f"Overall Performance Summary ({device_id_str}):\n\n"
    stats_text += f"Total Commands: {total_cmds}\n"
    stats_text += f"Successful Responses: {total_succ}\n"
    stats_text += f"Timeouts: {total_timeo}\n"
    if total_cmds > 0:
        stats_text += f"Success Rate: {overall_rate:.2f}%\n\n"
    else:
        stats_text += "Success Rate: N/A\n\n"

    if not valid_lat_df.empty:
        stats_text += "Latency (ms) for Successes:\n"
        stats_text += f"  Avg: {valid_lat_df['latency_ms'].mean():.2f}\n"
        stats_text += f"  Median: {valid_lat_df['latency_ms'].median():.2f}\n"
        stats_text += f"  Min: {valid_lat_df['latency_ms'].min():.2f}\n"
        stats_text += f"  Max: {valid_lat_df['latency_ms'].max():.2f}\n"
        stats_text += f"  95th Pctl: {valid_lat_df['latency_ms'].quantile(0.95):.2f}\n"
    else:
        stats_text += "Latency (ms) for Successes: N/A (No successful responses with latency)\n"

    plt.text(0.05, 0.95, stats_text, transform=plt.gca().transAxes,
             fontsize=9, verticalalignment='top', # Adjusted fontsize
             bbox=dict(boxstyle='round,pad=0.5', fc='aliceblue', alpha=0.9))
    plt.tight_layout(pad=1.0)
    plt.savefig(f"report_overall_summary_{device_id_str}.png")
    print(f"Saved image summary: report_overall_summary_{device_id_str}.png")
    plt.close()

def save_text_summary_report(filename, device_id_str, total_cmds, total_succ, total_timeo, overall_rate, valid_lat_df, relay_ids_list, all_records_df):
    """Saves a concise text summary report to a .txt file."""
    with open(filename, 'w', encoding='utf-8') as f:
        f.write(f"--- MQTT Relay Performance Summary ({device_id_str}) ---\n")
        f.write(f"Report generated on: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")

        f.write("=== Overall Performance ===\n")
        f.write(f"Total Commands Sent: {total_cmds}\n")
        f.write(f"Successful Responses: {total_succ}\n")
        f.write(f"Timeouts: {total_timeo}\n")
        if total_cmds > 0:
            f.write(f"Overall Success Rate: {overall_rate:.2f}%\n")
        else:
            f.write("Overall Success Rate: N/A\n")

        if not valid_lat_df.empty:
            f.write("\nOverall Latency (for successful responses):\n")
            f.write(f"  Average: {valid_lat_df['latency_ms'].mean():.2f} ms\n")
            f.write(f"  Median: {valid_lat_df['latency_ms'].median():.2f} ms\n")
            f.write(f"  Min: {valid_lat_df['latency_ms'].min():.2f} ms\n")
            f.write(f"  Max: {valid_lat_df['latency_ms'].max():.2f} ms\n")
            f.write(f"  95th Percentile: {valid_lat_df['latency_ms'].quantile(0.95):.2f} ms\n")
        else:
            f.write("\nOverall Latency: No successful responses with latency data.\n")

        f.write("\n=== Per Relay Summary ===\n")
        if not all_records_df.empty:
            for r_id in relay_ids_list:
                df_r = all_records_df[all_records_df['relay_id'] == r_id]
                if not df_r.empty:
                    relay_cmds = df_r.shape[0]
                    relay_succ = df_r[df_r['status'] == 'success'].shape[0]
                    relay_sr = (relay_succ / relay_cmds) * 100 if relay_cmds > 0 else 0

                    df_r_lat = df_r.dropna(subset=['latency_ms'])
                    avg_r_lat_val = df_r_lat['latency_ms'].mean() if not df_r_lat.empty else None
                    avg_r_lat_str = f"{avg_r_lat_val:.2f} ms" if avg_r_lat_val is not None else 'N/A'

                    f.write(f"\nRelay ID: {r_id}\n")
                    f.write(f"  Success Rate: {relay_sr:.2f}% ({relay_succ}/{relay_cmds} commands)\n")
                    f.write(f"  Average Latency: {avg_r_lat_str}\n")

                    # Add info about commands that implied change vs. no change for this relay
                    df_r_implied_change_true = df_r_lat[df_r_lat['command_implies_change'] == True]
                    df_r_implied_change_false = df_r_lat[df_r_lat['command_implies_change'] == False]

                    if not df_r_implied_change_true.empty:
                        avg_lat_change_true = df_r_implied_change_true['latency_ms'].mean()
                        f.write(f"    Avg Latency (state change cmd): {avg_lat_change_true:.2f} ms ({df_r_implied_change_true.shape[0]} cmds)\n")
                    if not df_r_implied_change_false.empty:
                        avg_lat_change_false = df_r_implied_change_false['latency_ms'].mean()
                        f.write(f"    Avg Latency (no change cmd): {avg_lat_change_false:.2f} ms ({df_r_implied_change_false.shape[0]} cmds)\n")

                else:
                    f.write(f"\nRelay ID: {r_id} - No data recorded.\n") # Should be rare if RELAY_IDS is consistent
        else:
             f.write("No data to summarize per relay.\n")

        f.write("\n--- End of Report ---")
    print(f"Text summary report saved to: {filename}")


# --- Generate Matplotlib Report and Text Summary ---
if not df_records.empty:
    try:
        plt.style.use('seaborn-v0_8-whitegrid')
    except OSError:
        print("Style 'seaborn-v0_8-whitegrid' not found, using 'ggplot' as fallback.")
        plt.style.use('ggplot')


    if not df_valid_latency.empty:
        print("\nGenerating Matplotlib reports for latency...")
        plot_latency_over_time(df_valid_latency, RELAY_IDS, DEVICE_ID)
        plot_average_latency_per_relay(df_valid_latency, DEVICE_ID)
        plot_latency_distribution(df_valid_latency, DEVICE_ID)
        plot_latency_by_scenario(df_valid_latency, DEVICE_ID)
        plot_command_change_vs_no_change_latency(df_valid_latency, DEVICE_ID) # New plot
    else:
        print("\nWarning: No successful latency data to generate latency-specific plots (e.g., all timeouts).")

    print("\nGenerating general reports (success rates, overall summary)...")
    plot_success_rate_per_relay(df_records, RELAY_IDS, DEVICE_ID) # Uses all_records_df
    save_text_summary_as_image(total_commands_sent_counter,
                               total_successful_responses,
                               total_timeouts,
                               overall_success_rate,
                               df_valid_latency, # Pass potentially empty df_valid_latency, handled by function
                               DEVICE_ID)

    text_report_filename = f"report_summary_{DEVICE_ID}_{datetime.datetime.now().strftime('%Y%m%d_%H%M%S')}.txt"
    save_text_summary_report(text_report_filename, DEVICE_ID,
                             total_commands_sent_counter,
                             total_successful_responses,
                             total_timeouts,
                             overall_success_rate,
                             df_valid_latency, # Pass potentially empty df_valid_latency
                             RELAY_IDS,
                             df_records)

elif df_records.empty:
    print("\nWarning: No data recorded. Cannot generate any reports or save CSV.")


print("\nScript execution completed!")
