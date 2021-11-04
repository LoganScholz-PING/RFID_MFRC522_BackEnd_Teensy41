#include <SPI.h>
#include <MFRC522.h>
#include <Arduino.h>

// MFRC522 Vars
#define RST_PIN         9           // Configurable, see typical pin layout above
#define SS_PIN          10          // Configurable, see typical pin layout above

MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance

// serial comm vars
#define SOP '<' // denotes start of serial data packet
#define EOP '>' // denotes end of serial data packet

char ctrlChar;
char serialData[20];     // serial receive buffer
byte idx;              // serialData indexer
boolean started = false; // serial data flow control
boolean ended   = false;   // serial data flow control


void setup() 
{
  Serial.begin(115200);                                         // Initialize serial communications with the PC
  SPI.begin();                                                  // Init SPI bus
  mfrc522.PCD_Init();                                           // Init MFRC522 card
  //Serial.println("[INFO] Finished Initialization.");
}


/* ***********************************************************
 * FUNCTION NAME: readRFID()
 *    
 *    SUMMARY: read the workorder block from the RFID (if present)
 * ***********************************************************/
void readRFID()
{
  // Prepare key - all keys are set to FFFFFFFFFFFFh at chip delivery from the factory.
  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  //some variables we need
  bool skipnormalconn = false;
  byte block;
  MFRC522::StatusCode status;
  MFRC522::StatusCode wakeupstatus;
  byte bufferATQA1[2];
  byte bufferSize1 = 2;
  byte buffer2[18]; // must be minimum 18 for MIFARE_Read() -- 16 bytes for block and 2 bytes for CRC
  
  byte len = 18;

  // *************** TEST CARD WAKE-UP AND RE-SELECTION **************
  // *************** (try to move HALT cards to READY* state)
  // ***** HALT -> READY* = WUPA -> SELECT (should be READY* after SELECT)

  // Look for new cards and attempt to wake up sleeping cards
  if ( ! mfrc522.PICC_IsNewCardPresent() ) 
  {
    // card may be in HALT state so let's try to wake it up (move it to READY state)
    // if this fails there is probably no card present on the reader
    wakeupstatus = mfrc522.PICC_WakeupA(bufferATQA1, &bufferSize1);

    if ( wakeupstatus == MFRC522::STATUS_OK )
    {
      //Serial.println("[INFO] PICC_WakeupA status OK! Attempting to Select.");
      wakeupstatus = mfrc522.PICC_Select(&(mfrc522.uid), mfrc522.uid.size);

      if ( ! wakeupstatus == MFRC522::STATUS_OK )
      {
        Serial.print(F("[ERROR] Failed to wake up tag. Status code = "));
        Serial.println(mfrc522.GetStatusCodeName(wakeupstatus));
        return;
      }
      else
      {
        // we have successfully reset communication with the card
        skipnormalconn = true;
      }
    }
    else
    {
      Serial.println(F("[INFO] Attempt to read but no working tag present!"));
      return;
    }
  }
  // *************** END TEST **************


  // Select one of the cards
  if (! skipnormalconn)
  {
    if ( ! mfrc522.PICC_ReadCardSerial()) 
    {
      Serial.println(F("[INFO] PICC_ReadCardSerial() Failed!"));
      return;
    }
  }
  

  //---------------------------------------- GET WORKORDER

  status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, 1, &key, &(mfrc522.uid)); //line 834
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Authentication failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }

  block = 1;

  status = mfrc522.MIFARE_Read(block, buffer2, &len);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Reading failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }

  // --------------------------------  

  //Serial.println(F("[INFO] ATTEMPTING TO READ WORKORDER"));

  //Serial.print("[INFO] Size of buffer2: ");
  //Serial.println(sizeof(buffer2));

  if (sizeof(buffer2) > 0)
  {
    char wo[11];

    // we are here because the read operation was successful
    // and buffer2 contains data (buffer2 contains all bytes of block 1)
    //    the only bytes of buffer2 we care about are 0-6

    // start building response serial packet:
    wo[0] = '<';
    wo[1] = 'D';
    wo[2] = ':';

    for (uint8_t j = 0; j < 7; j++)
    {
      if (buffer2[j] == 32 || buffer2[j] == 0) // dec 32 = hex 0x20 = ASCII "SPACE"  || dec 0 = hex 0x00 = NULL
      {
        // get rid of blanks or other non-standard ASCII characters
        //  - replace them with highly visible ASCII character so C#
        //    can easily check for this on the front-end
        wo[j+3] = '#';
      }
      else
      {
        // data is normal (hopefully), throw it in our workorder char array
        wo[j+3] = (char)buffer2[j];
      }
    } 

    wo[10] = (char)'>';

    for (uint8_t i = 0; i < 11; i++)
    {
      Serial.write((byte)wo[i]);
    }

    // use the decimal value of the ASCII character
    // (we need to manually insert CR/LF to the serial
    // packet because Serial.write() does not automatically
    // insert them like Serial.println() does)
    //
    // Note that we can probably just use Serial.println(); 
    // here, but I want to experiment
    Serial.write(13); // 13 = CR
    Serial.write(10); // 10 = LF
  }
  else
  {
    Serial.println(F("[ISSUE] buffer2 <= 0 in READ operation! How??"));
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}






/* ***********************************************************
 * FUNCTION NAME: writeRFID()
 *    
 *    SUMMARY: write a 7 digit workorder to block1 + block2 
 * ***********************************************************/
void writeRFID()
{
  //Serial.println(F("[INFO] BEGINNING WRITE PROCESS! DO NOT MOVE PICC!"));    // print to serial

  // Prepare key - all keys are set to FFFFFFFFFFFFh at chip delivery from the factory.
  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  // Look for new cards
  if ( ! mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  // Select one of the cards
  if ( ! mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  //Serial.print(F("Card UID:"));    //Dump UID
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    //Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
    //Serial.print(mfrc522.uid.uidByte[i], HEX);
  }

  //Serial.print(F(" PICC type: "));   // Dump PICC type
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  //Serial.println(mfrc522.PICC_GetTypeName(piccType));

  byte buffer[34];
  byte block;
  MFRC522::StatusCode status;
  byte len;

  //Serial.setTimeout(20000L) ;     // wait until 20 seconds for input from serial
  // Ask personal data: Family name
  
    /* !!!!!!! TODO !!!!!!!
     * This is where the workorder from C# will be pushed in
     * (instead of reading from serial)
     * 
     * Question:
     * 1. Do we NEED 2 blocks if we know that we are only storing a 
     *    7 digit integer #?
     */

  //Serial.println(F("Type workorder, ending with # (ex: \"1234567#\""));
  len = Serial.readBytesUntil('#', (char *) buffer, 30) ; // read workorder # from serial
  for (byte i = len; i < 30; i++) buffer[i] = ' ';     // pad with spaces

  block = 1;
  ////Serial.println(F("Authenticating using key A..."));
  status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("PCD_Authenticate() (block 1) failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }
  else
  {
    //Serial.println(F("PCD_Authenticate() (block 1) success: "));
  }

  // Write block
  status = mfrc522.MIFARE_Write(block, buffer, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Write() workorder to block 1 failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }
  else
  {
    //Serial.println(F("Write() workorder to block 1 success!"));
  } 

  block = 2;
  ////Serial.println(F("Authenticating using key A..."));
  status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("PCD_Authenticate() (block 2) failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }

  // Write block
  status = mfrc522.MIFARE_Write(block, &buffer[16], 16);
  if (status != MFRC522::STATUS_OK) {
    //Serial.print(F("Write() block 2 failed: "));
    //Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }
  else
  {
    //Serial.println(F("Write() block 2 success!"));
  } 

  // // Ask personal data: First name
  // //Serial.println(F("Type First name, ending with #"));
  // len = //Serial.readBytesUntil('#', (char *) buffer, 20) ; // read first name from serial
  // for (byte i = len; i < 20; i++) buffer[i] = ' ';     // pad with spaces

  // block = 4;
  // ////Serial.println(F("Authenticating using key A..."));
  // status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
  // if (status != MFRC522::STATUS_OK) {
  //   //Serial.print(F("PCD_Authenticate() failed: "));
  //   //Serial.println(mfrc522.GetStatusCodeName(status));
  //   return;
  // }

  // // Write block
  // status = mfrc522.MIFARE_Write(block, buffer, 16);
  // if (status != MFRC522::STATUS_OK) {
  //   //Serial.print(F("MIFARE_Write() failed: "));
  //   //Serial.println(mfrc522.GetStatusCodeName(status));
  //   return;
  // }
  // else //Serial.println(F("MIFARE_Write() success: "));

  // block = 5;
  // ////Serial.println(F("Authenticating using key A..."));
  // status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
  // if (status != MFRC522::STATUS_OK) {
  //   //Serial.print(F("PCD_Authenticate() failed: "));
  //   //Serial.println(mfrc522.GetStatusCodeName(status));
  //   return;
  // }

  // // Write block
  // status = mfrc522.MIFARE_Write(block, &buffer[16], 16);
  // if (status != MFRC522::STATUS_OK) {
  //   //Serial.print(F("MIFARE_Write() failed: "));
  //   //Serial.println(mfrc522.GetStatusCodeName(status));
  //   return;
  // }
  // else 
  // {
  //   //Serial.println(F("MIFARE_Write() success!"));
  // } 
  
  //Serial.println(" ");

  mfrc522.PICC_HaltA(); // Halt PICC
  mfrc522.PCD_StopCrypto1();  // Stop encryption on PCD
}


void checkSerial() {
  // if serial is available, receive the
  // serial data packet (it better be formatted
  // correctly!!)
  while (Serial.available() > 0) {
    char inChar = Serial.read();
    // check if we receive the start character  
    // (SOP) of a serial packet
    if (inChar == SOP) {
      idx = 0;
      serialData[idx] = '\0'; // null character
      started = true;
      ended = false;
    }
    // check if we receive the end character
    // (EOP) of a serial packet
    else if (inChar == EOP) {
      ended = true;
      break;
    }
    else
    {
      if (idx < 79) {
        serialData[idx] = inChar;
        ++idx;
        serialData[idx] = '\0';
      }
    }
  }
  if (started && ended) {
    // packet start and end control characters
    // received, begin packet processing
    switch (serialData[0]) {
      case 'W':
        // STRIP OUT W/O THEN WRITE IT
        // The "write" packet from C# looks like this:
        //      [0123456789]
        //      "<W:1234567>"
        writeRFID();
        break;
      case 'R':
        // READ W/O FROM CARD
        readRFID();
        break;
      default:
        // we received a packet where serialData[0] 
        // is unrecognized
        //Serial.print("ERROR Invalid or uninterpretable command received on serial: ");
        //Serial.println(serialData[0]);
        break;
    }
    // packet processing completed, reset packet
    // parameters to get ready for next packet
    started = false;
    ended = false;
    idx = 0;
    serialData[idx] = '\0';
  }
}


void loop() 
{
  //
  checkSerial();

  // if you don't the 2 lines below then the Teensy wastes ~ 30-40mA continuously and gets hot
  // when you don't do these things Teensy uses 110-120mA continuously
  // when you DO these things the Teensy uses 80-90mA continuously
  mfrc522.PICC_HaltA(); 
  mfrc522.PCD_StopCrypto1();
}