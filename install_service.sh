#!/bin/bash

# Ensure the script is run with sudo
if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root. Use 'sudo' to execute it."
   exit 1
fi

# Define the source and destination paths
SOURCE="/home/admin/home/terramaster-fancontrol/fancontrol.service"
DESTINATION="/etc/systemd/system/fancontrol.service"

# Step 1: Copy the service file
echo "Copying $SOURCE to $DESTINATION..."
if cp "$SOURCE" "$DESTINATION"; then
    echo "Successfully copied $SOURCE to $DESTINATION."
else
    echo "Failed to copy $SOURCE to $DESTINATION. Exiting."
    exit 1
fi

# Step 2: Daemon reload
echo "Reloading daemon..."
if systemctl daemon-reload; then
    echo "Daemon reloaded."
else
    echo "Failed to reload daemon. Exiting."
    exit 1
fi

# Step 3: Stop the service
echo "Stopping fancontrol.service..."
if systemctl stop fancontrol.service; then
    echo "fancontrol.service stopped successfully."
else
    echo "Failed to stop fancontrol.service. Exiting."
    exit 1
fi

# Step 4: Start the service
echo "Starting fancontrol.service..."
if systemctl start fancontrol.service; then
    echo "fancontrol.service started successfully."
else
    echo "Failed to start fancontrol.service. Exiting."
    exit 1
fi

# Step 5: Enable the service
echo "Enabling fancontrol.service..."
if systemctl enable fancontrol.service; then
    echo "fancontrol.service enabled successfully."
else
    echo "Failed to enable fancontrol.service. Exiting."
    exit 1
fi

# Step 6: Display the service status
echo "Checking status of fancontrol.service..."
systemctl status fancontrol.service

exit 0
