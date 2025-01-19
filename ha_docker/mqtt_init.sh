#!/bin/sh

# Set default username and password
MQTT_USERNAME=${MQTT_USERNAME:-mqtt_user}
MQTT_PASSWORD=${MQTT_PASSWORD:-password}
PASSWORD_FILE="/mosquitto/config/password_file"

# Check if the password file exists
if [ ! -f "$PASSWORD_FILE" ]; then
  echo "Creating Mosquitto password file..."
  touch "$PASSWORD_FILE"
fi

# Add the user to the password file (overwriting if it exists)
echo "Adding MQTT user: $MQTT_USERNAME"
mosquitto_passwd -b "$PASSWORD_FILE" "$MQTT_USERNAME" "$MQTT_PASSWORD"

# Ensure correct permissions
chmod 600 "$PASSWORD_FILE"

echo "Initialization complete."
