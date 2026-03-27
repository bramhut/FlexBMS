import pandas as pd
import matplotlib.pyplot as plt

# Load the CSV file
file_path = 'data.csv'
data = pd.read_csv(file_path)

# Convert the timestamp to a more readable format (assuming it is in milliseconds since epoch)
data['timestamp'] = pd.to_datetime(data['timestamp'], unit='ms')

# Extract the columns related to individual cell voltages and their balancing states
cell_voltage_columns = [col for col in data.columns if col.startswith('cell_1_')]
balancing_columns = [col for col in data.columns if col.startswith('bal_1_')]

# Plot the individual cell voltages over time and include the pack current
fig, ax1 = plt.subplots(figsize=(12, 6))

# Plotting the individual cell voltages with different line styles based on balancing state
for col, bal_col in zip(cell_voltage_columns, balancing_columns):
    balanced = data[bal_col] == 1
    ax1.plot(data['timestamp'], data[col].where(~balanced), label=f'{col} (not balanced)')
    ax1.plot(data['timestamp'], data[col].where(balanced), linestyle=':', label=f'{col} (balanced)')

# Setting up the first y-axis for cell voltages
ax1.set_xlabel('Time')
ax1.set_ylabel('Cell Voltage (V)')
ax1.set_title('Individual Cell Voltages, Pack Current, and Balancing State Over Time')
ax1.legend(loc='upper right', bbox_to_anchor=(1.3, 1))
ax1.grid(True)

# Creating a second y-axis for the pack current
# ax2 = ax1.twinx()
# ax2.plot(data['timestamp'], data['pack_current'], label='Pack Current', color='black', linestyle='--', linewidth=1.5)
# ax2.set_ylabel('Pack Current (A)')
ax2 = ax1.twinx()
ax2.plot(data['timestamp'], data['pack_voltage'], label='Pack Voltage', color='black', linestyle='--', linewidth=1.5)
ax2.set_ylabel('Pack Voltage (v)')

# Adding a legend for the pack current
lines, labels = ax1.get_legend_handles_labels()
lines2, labels2 = ax2.get_legend_handles_labels()
ax2.legend(lines + lines2, labels + labels2, loc='upper left')

# Display the plot
plt.show()
