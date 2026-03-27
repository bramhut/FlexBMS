import pandas as pd
import statistics

# setpoint = [3.36, 3.35, 3.34, 3.33, 3.32, 3.31, 3.30, 3.29, 3.28, 3.27, 3.26, 3.25]
setpoint = [3.25, 3.26, 3.27, 3.28, 3.29, 3.30, 3.31, 3.32, 3.33, 3.34, 3.35, 3.36]
def avgCellVoltageFromCsv(file, columnStart):
    averages = []
    sd = []
    # First load companion
    # Load the data from a CSV file
    data = pd.read_csv(file)

    # Filter columns that start with 'cell_0_'
    columns = [col for col in data.columns if col.startswith(columnStart)]

    # Calculate and print the average for each column
    # averages = {}
    for col in columns:
        averages.append(data[col].mean())
        sd.append(statistics.stdev(data[col]))

    return averages, sd

def createMeasurementSubPlot(subplot, setpoint, companion_avgs, hioki_avgs):
    n = len(setpoint)

    # X-axis indices for the arrays
    indices = np.arange(n)

    # Width of each bar
    bar_width = 0.25

    # Create the bar chart
    # subplot.bar(indices, setpoint, width=bar_width, label='setpoint', color='b', align='center')
    subplot.bar(indices + bar_width, companion_avgs, width=bar_width, label='Standard', color='g', align='center')
    subplot.bar(indices + 2 * bar_width, hioki_avgs, width=bar_width, label='Ground fix applied', color='r', align='center')

    # Add labels, title, and legend
    subplot.ylim(3.291, 3.305)
    subplot.xlabel('Cell number')
    subplot.ylabel('Voltage [V]')
    subplot.title('BMS cell voltage measurement comparison with and without ground fix applied')
    subplot.xticks(indices + bar_width, indices)
    subplot.legend()

def createDiffSubPlot(subplot ,diffs):
    n = len(diffs)
    bar_width = 0.25

    # X-axis indices for the arrays
    indices = np.arange(n)
    # subplot.show()
    subplot.bar(indices, diffs, width=bar_width, color='b', align='center')
    # plt.ylim(3.23, 3.37)
    subplot.xlabel('Cell number')
    subplot.ylabel('Voltage [mV]')
    subplot.title('Difference between BMS and Cell generator')
    subplot.xticks(indices + bar_width, indices)
    subplot.legend()
    # subplot.show()



companion_avgs, companion_sd = avgCellVoltageFromCsv('companion_no_avg_no_gnd.csv', 'cell_0_')
companion_avgs_fix, companion_sd_fix = avgCellVoltageFromCsv('companion_no_avg_gnd_fix.csv', 'cell_0_')
# hioki_avgs_reverse, hioki_sd_reverse = avgCellVoltageFromCsv('hioki_no_avg.csv', 'Unit1_Ch')
# hioki_avgs, hioki_sd = hioki_avgs_reverse[:-12][::-1], hioki_sd_reverse[:-12][::-1]
# hioki_avgs = avgCellVoltageFromCsv('hioki_16_avg.csv', 'Unit1_Ch')[:-12][::-1]

# diffs = []
# for i, val in enumerate(companion_avgs):
#     print("Setpoint: " + str(setpoint[i]) + " Companion: " + str(companion_avgs[i]) + " Hioki: " + str(hioki_avgs[i]) + " Diff: " + str(companion_avgs[i] - hioki_avgs[i]))
#     # Print difference to setpoint for both companion and hioki:
#     print("Setpoint diff: Companion: " + str(companion_avgs[i] - setpoint[i]) + " Hioki: " + str(hioki_avgs[i] - setpoint[i]) + " Diff: " + str(companion_avgs[i] - hioki_avgs[i]))
#     # print("Companion: " + str(companion_avgs[i]) + " Hioki: " + str(hioki_avgs[i]) + " Diff: " + str(companion_avgs[i] - hioki_avgs[i]))
#     diffs.append((hioki_sd[i] - companion_sd[i]) * 1000)

import matplotlib.pyplot as plt
import numpy as np

# Sample data: Replace these with your actual data


# Length of the arrays
# fig, axs = plt.subplots(2)

plt.figure(1)
createMeasurementSubPlot(plt, setpoint, companion_avgs, companion_avgs_fix)

# plt.figure(2)
# createDiffSubPlot(plt, diffs)

# show plot
plt.show()

# # Show the plot
