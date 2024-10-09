unsigned char buffer[1];

void setup() {
  Serial.begin(9600);
}

void loop() {
  // If a byte is being sent
  if(Serial.available() > 0) {
    Serial.readBytes(buffer, 1);  // Save the byte to the buffer
    if(buffer[0] != 10) {  // Skips the byte if it's an endline character
      Serial.println(buffer[0]);
    }
    
  }
}
