#include <RCSwitch.h>
#include <BluetoothSerial.h>
#include <SPIFFS.h>

RCSwitch mySwitch;
BluetoothSerial SerialBT;

const int receiverPin = 2;      // Receiver pin (GPIO2)
const int transmitterPin = 4;   // Transmitter pin (GPIO4)
const int ledPin = 13;          // LED pin (GPIO13)
int tempF = 0;
int hall = 0;
int tempC = 0;
bool sensorLoop = false;

// Define the maximum number of signals to be stored
const int maxSignals = 50;

// Define the maximum number of presaved signals
const int maxPresavedSignals = 10;

// Structure to represent a signal
struct Signal {
  int id;
  unsigned long value;
  bool isPermanent;
};

// Structure to represent a presaved signal
struct PresavedSignal {
  int id;
  unsigned long value;
  String name;  // Name of the presaved signal
};

// Array to store recorded signals
Signal recordedSignals[maxSignals];
int numRecordedSignals = 0;

// Array to store presaved signals
PresavedSignal presavedSignals[maxPresavedSignals];
int numPresavedSignals = 0;

// Password for memory wipe
const String memoryWipePassword = "Memory";  // Change this to your desired password

void setup() {
  Serial.begin(115200);
  SerialBT.begin("Signal Emulator"); // Bluetooth device name

  mySwitch.enableReceive(receiverPin);
  mySwitch.enableTransmit(transmitterPin);
  pinMode(ledPin, OUTPUT);

  // Mount SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Error mounting SPIFFS");
    return;
  }

  loadSavedSignals();
  setupPresavedSignals(); // Initialize presaved signals
}

void loop() {
  tempF = temperatureRead();
  hall = hallRead();
  tempC = (tempF - 32) / 1.8;
  Sensors();

  if (mySwitch.available()) {
    unsigned long receivedValue = mySwitch.getReceivedValue();

    if (receivedValue != 0 && numRecordedSignals < maxSignals) {
      int signalId = numRecordedSignals++; // Assign the current count and then increment it

      recordedSignals[signalId] = {signalId, receivedValue, false}; // Assume all new signals are temporary

      SerialBT.println("Saved signal with ID " + String(signalId) + ": " + String(receivedValue));

      digitalWrite(ledPin, HIGH);
      delay(500);
      digitalWrite(ledPin, LOW);
      delay(100);
      digitalWrite(ledPin, HIGH);
      delay(500);
      digitalWrite(ledPin, LOW);

      mySwitch.resetAvailable();
    }
  }

  if (SerialBT.available()) {
    String command = SerialBT.readStringUntil('\n'); // Read the entire line
    command.trim();  // Remove leading and trailing whitespaces

    if (command.startsWith("list")) {
      listSignals();
    } 
    
    else if (command.startsWith("send")) {
      int signalId = command.substring(1).toInt();

      if (signalId >= 0 && signalId < numRecordedSignals) {
        digitalWrite(ledPin, HIGH);
        mySwitch.send(recordedSignals[signalId].value, 24);
        digitalWrite(ledPin, LOW);
        SerialBT.println("Sent signal with ID " + String(signalId) + ": " + String(recordedSignals[signalId].value));
        delay(500); // Add a delay to ensure the message is sent before the reset
        mySwitch.resetAvailable(); // Reset the available status to avoid re-sending the same signal
      } else {
        SerialBT.println("Signal ID not found!");
      }
    } 
    
    else if (command.startsWith("unsave")) {
      int signalId = command.substring(1).toInt();

      if (signalId >= 0 && signalId < numRecordedSignals && recordedSignals[signalId].isPermanent) {
        // Change the status of the permanent signal to temporary
        recordedSignals[signalId].isPermanent = false;
        saveSignals();
        SerialBT.println("Unsaved permanent signal with ID " + String(signalId));
      } 
      
      else {
        SerialBT.println("Permanent signal ID not found or signal is already temporary!");
      }
    } 
    
    else if (command.startsWith("save")) {
      int signalId = command.substring(4).toInt();

      if (signalId >= 0 && signalId < numRecordedSignals && !recordedSignals[signalId].isPermanent) {
        recordedSignals[signalId].isPermanent = true;
        saveSignals();
        SerialBT.println("Saved signal with ID " + String(signalId) + " permanently.");
      } 
      
      else {
        SerialBT.println("Signal ID not found or signal is already permanent!");
      }
    }

    else if (command.startsWith("info")) {
      int signalId = command.substring(4).toInt();

      if (signalId >= 0 && signalId < numRecordedSignals) {
        displaySignalInfo(recordedSignals[signalId]);
      } else {
        SerialBT.println("Signal ID not found!");
      }
    }

    else if (command.startsWith("PS list")) {
      listPresavedSignals();
    } 
    else if (command.startsWith("PS")) {
      String presavedSignalName = command.substring(3);
      sendPresavedSignal(presavedSignalName);
    }
    else if (command.startsWith ("sensors")) {
      sensorLoop = !sensorLoop;
    }
    else if (command.startsWith ("help")) {
      SerialBT.println(" List of commands:");
      SerialBT.println();
      SerialBT.println("sensors:");
      SerialBT.println("  lists output of built-in sensors");
      SerialBT.println("list:");
      SerialBT.println("  lists saved signals and their ID's");
      SerialBT.println("send(signal ID):");
      SerialBT.println("  sends the signal");
      SerialBT.println("unsave(signalID):");
      SerialBT.println("  unsaves the permanent signal");
      SerialBT.println("save(signalID):");
      SerialBT.println("  saves the signal permanently");
      SerialBT.println("info(signalID):");
      SerialBT.println("  displays information about the signal");
      SerialBT.println("PS list:");
      SerialBT.println("  lists presaved signals and their ID's");
      SerialBT.println("PS (signalName):");
      SerialBT.println("  sends the presaved signal by name");
      SerialBT.println("memory wipe (password)");
      SerialBT.println("  wipes all saved memory on the device");
      SerialBT.println("info (signal ID):");
      SerialBT.println("  displays all possible info on signal");
    }

    else if (command.startsWith("memory wipe")) {
      // Extract the password from the command
      String enteredPassword = command.substring(12); // Adjust the number based on the length of "memory wipe "

      if (enteredPassword.equals(memoryWipePassword)) {
        SerialBT.println("Password correct. Wiping memory...");
        wipeMemory();
        SerialBT.println("Memory wipe completed.");
      }
      else {
        SerialBT.println("Incorrect password. Memory wipe aborted.");
      }
    }
  }
}

void saveSignals() {
  File file = SPIFFS.open("/signals.dat", "w");

  if (!file) {
    Serial.println("Error opening file for writing");
    return;
  }

  for (int i = 0; i < numRecordedSignals; ++i) {
    if (recordedSignals[i].isPermanent) {
      file.write((uint8_t*)&recordedSignals[i], sizeof(recordedSignals[i]));
    }
  }

  file.close();
}

void listSignals() {
  SerialBT.println("List of saved signals:");

  for (int i = 0; i < numRecordedSignals; ++i) {
    SerialBT.print("ID: ");
    SerialBT.print(recordedSignals[i].id);
    SerialBT.print(", Signal: ");
    SerialBT.print(recordedSignals[i].value);
    SerialBT.print(", Permanent: ");
    SerialBT.println(recordedSignals[i].isPermanent ? "Yes" : "No");
  }
}

void displaySignalInfo(Signal signal) {
  SerialBT.println("Signal ID: " + String(signal.id));
  SerialBT.println("Signal Value: " + String(signal.value));
  SerialBT.println("Permanent: " + String(signal.isPermanent ? "Yes" : "No"));

  // Additional information
  SerialBT.println("Binary: " + String(signal.value, BIN));
  SerialBT.println("Octal: " + String(signal.value, OCT));
  SerialBT.println("Hexadecimal: 0x" + String(signal.value, HEX));
  SerialBT.println("Decimal: " + String(signal.value));
  SerialBT.println("Tetra Bits: " + String(signal.value, 4)); // Specify the number of digits (tetrabits)
  SerialBT.println("Trit Bits: " + String(signal.value, 3));  // Specify the number of digits (tritbits)

  // Add any other information you want to display
}

void Sensors() {
  if (sensorLoop == true) {
    SerialBT.print("Temperature: ");
    SerialBT.print(tempC);
    SerialBT.print("C    ");
    SerialBT.print(tempF);
    SerialBT.println("F");
    SerialBT.print("Hall Effect Sensor: ");
    SerialBT.println(hall);
    delay(1000);
  }
}

void loadSavedSignals() {
  File file = SPIFFS.open("/signals.dat", "r");
  if (file) {
    numRecordedSignals = 0; // Reset the count
    while (file.available() && numRecordedSignals < maxSignals) {
      Signal signal;
      file.read((uint8_t*)&signal, sizeof(signal));
      recordedSignals[numRecordedSignals++] = signal;
    }
    file.close();
  }
}

void wipeMemory() {
  // Wipe the saved signals and reset the count
  numRecordedSignals = 0;
  for (int i = 0; i < maxSignals; ++i) {
    recordedSignals[i] = {0, 0, false};
  }

  // Wipe the saved signals file
  SPIFFS.remove("/signals.dat");
}

void setupPresavedSignals() {
  // Initialize presaved signals with names
  // You can customize the names and values as needed
  presavedSignals[0] = {0, 123456, "on"};
  presavedSignals[1] = {1, 654321, "off"};
  presavedSignals[2] = {2, 987654, "toggle"};
  presavedSignals[3] = {3, 111222, "dim"};
  presavedSignals[4] = {4, 333444, "bright"};
  presavedSignals[5] = {5, 555666, "open"};
  presavedSignals[6] = {6, 777888, "close"};
  presavedSignals[7] = {7, 999000, "start"};
  presavedSignals[8] = {8, 444333, "stop"};
  presavedSignals[9] = {9, 666555, "pause"};

  numPresavedSignals = maxPresavedSignals;
}

void listPresavedSignals() {
  SerialBT.println("List of presaved signals:");

  for (int i = 0; i < numPresavedSignals; ++i) {
    SerialBT.print("ID: ");
    SerialBT.print(presavedSignals[i].id);
    SerialBT.print(", Name: ");
    SerialBT.print(presavedSignals[i].name);
    SerialBT.print(", Signal: ");
    SerialBT.println(presavedSignals[i].value);
  }
}

void sendPresavedSignal(String presavedSignalName) {
  for (int i = 0; i < numPresavedSignals; ++i) {
    if (presavedSignals[i].name.equals(presavedSignalName)) {
      digitalWrite(ledPin, HIGH);
      mySwitch.send(presavedSignals[i].value, 24);
      digitalWrite(ledPin, LOW);
      SerialBT.println("Sent presaved signal with name '" + presavedSignalName + "': " + String(presavedSignals[i].value));
      delay(500); // Add a delay to ensure the message is sent before the reset
      mySwitch.resetAvailable(); // Reset the available status to avoid re-sending the same signal
      return;
    }
  }

  SerialBT.println("Presaved signal with name '" + presavedSignalName + "' not found!");
}
