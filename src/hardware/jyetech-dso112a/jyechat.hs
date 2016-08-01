#! /usr/bin/env runhaskell
{-# LANGUAGE OverloadedStrings #-}
{-# LANGUAGE LambdaCase #-}

{- Just chat with the jyetech DSO

   This reads from arg1, pulls in data and prints the results. If it gets a
   recognizable packet, it'll try and parse it. Otherwise, it dumps hex
   and ascii if the byte is printable.

   You can send queries/commands to the DSO as:
	TBD
-}


import Control.Monad (when, replicateM)
import qualified Data.ByteString as B
import qualified Data.ByteString.Char8 as BC
import Data.List (intercalate)
import Data.Word8 (Word8)
import Safe (headNote)
import System.Environment (getArgs)
import System.Exit (ExitCode(ExitSuccess), exitWith)
import System.Hardware.Serialport (SerialPort, CommSpeed(CS115200), commSpeed,
                                   defaultSerialSettings, openSerial, flush,
                                   recv, send)
import System.IO (Handle, IOMode(ReadWriteMode, WriteMode), SeekMode(AbsoluteSeek),
                  hIsOpen, hPutStrLn, hReady, hSeek, stdin,
                  withBinaryFile, withFile)

main :: IO ()
main = do
  args <- getArgs
  port <- openSerial (headNote "Must provide serial port argument" args)
                     defaultSerialSettings {commSpeed = CS115200}
  withBinaryFile "/tmp/jyetech.binary" ReadWriteMode $ loopOn port

loopOn :: SerialPort -> Handle -> IO ()
loopOn port handle = do
  -- check for sync byte and handle packet, otherwise print byte if we got one
  recv port 1 >>= \case
                     ""     -> return ()
                     "\254" -> handlePacket port handle
                     x      -> putStrLn $ "Got: " ++ show x
    
  -- check for and run commands
  hReady stdin >>= flip when (runCommand port)

  -- and back for more
  loopOn port handle
      
handlePacket :: SerialPort -> Handle -> IO ()
handlePacket port handle = do
  frameId <- B.head <$> recv port 1
  if frameId == 0x00 then
      putStrLn "Ignoring byte stuffed sync byte."
  else do
    loByte <- getStuffed port
    hiByte <- getStuffed port
    let frameSize = toInteger hiByte * 256 + toInteger loByte
    payload <- getPayload port $ fromIntegral (frameSize - 3)
    case frameId of
      0xC0 -> case B.head payload of
                0x31 -> dumpParms payload
                0x30 -> dumpConfig payload
                0x32 -> recv port 1 >> dumpCapture payload (frameSize - 8) handle
                0x33 -> dumpSample payload handle
                0x34 -> dumpMeasurements payload
                _    -> dumpFrameInfo frameId payload
      0xE2 -> dumpDevice payload
      0xE6 -> dumpData handle
      _    -> dumpFrameInfo frameId payload

getStuffed :: SerialPort -> IO Word8
getStuffed port = do
    byte <- B.head <$> recv port 1
    when (byte == 0xFE) $ do
         new <- B.head <$> recv port 1
         if new /= 0 then putStrLn $ "Discarded bad stuffing byte " ++ show new
         else putStrLn "Discarded good stuffing byte"
    return byte

getPayload :: SerialPort -> Int -> IO B.ByteString
getPayload port count = B.pack <$> replicateM count (getStuffed port)

dumpParms :: B.ByteString -> IO ()
dumpParms string = do
  let payload = B.unpack string
  dumpValue "VSen" $ getByte payload 1
  dumpValue "Cpl" $ getByte payload 2
  dumpValue "VPos" $ getSignedWord payload 3
  dumpValue "TimeBase" $ getByte payload 9
  dumpValue "TrigMode" $ getByte payload 13
  dumpValue "TrigSlope" $ getByte payload 14
  dumpValue "TrigLvl" $ getWord payload 15
  dumpValue "TrigPos" $ getByte payload 17
  dumpValue "TrigSrc" $ getByte payload 18
  dumpValue "Measurements" $ getByte payload 20
  dumpValue "RecordLength" $ getLong payload 21
  dumpValue "HPos" $ getLong payload 27
  putStrLn ""

dumpConfig :: B.ByteString -> IO ()
dumpConfig string = do
  let payload = B.unpack string
  dumpMinMax "Vsen" $ getByteMinMax payload 1
  dumpMinMax "Cpl" $ getByteMinMax payload 3
  dumpMinMax "VPos" $ getSignedWordMinMax payload 5
  dumpMinMax "TimeBase" $ getByteMinMax payload 17
  dumpMinMax "TrigMode" $ getByteMinMax payload 23
  dumpMinMax "TrigSlope" $ getByteMinMax payload 25
  dumpMinMax "TrigLvl" $ getSignedWordMinMax payload 27
  dumpMinMax "TrigPos" $ getByteMinMax payload 31
  dumpMinMax "TrigSrc" $ getByteMinMax payload 33
  dumpMinMax "RecLen" $ getLongMinMax payload 39
  putStrLn ""

dumpCapture :: B.ByteString -> Integer -> Handle -> IO ()
dumpCapture string size handle = do
  putStr "."
  B.hPut handle $ B.take (fromInteger size) (BC.drop 1 string)

dumpSample :: B.ByteString -> Handle -> IO ()
dumpSample string handle = do
  putStr "."
  BC.hPut handle (B.singleton $ B.index string 1)

dumpMeasurements :: B.ByteString -> IO ()
dumpMeasurements string = do
  let payload = B.unpack string
  let vbu = getByte payload 1
  let vres = getWord payload 3
  let toVolts x = fromIntegral (x * vres * vbu) / 1000000.0
  dumpValue "VBU" $ vbu
  dumpValue "VRes" $ vres
  dumpValue "Vmax" . toVolts $ getSignedWord payload 5
  dumpValue "Vmin" . toVolts $ getSignedWord payload 7
  dumpValue "Vpp"  . toVolts $ getSignedWord payload 9
  dumpValue "Vavr" . toVolts $ getSignedWord payload 11
  dumpValue "Vrms" . toVolts $ getSignedWord payload 13
  dumpValue "Hz" $ getLong payload 17
  putStrLn ""

dumpDevice :: B.ByteString -> IO ()
dumpDevice string = do
    putStrLn $ "Device type " ++ devType ++ " model " ++ model ++ ": " ++ name
                 ++ " version: " ++ version
    where devType = show $ BC.head string
          model = show $ B.index string 1
          name = BC.unpack . fst . BC.breakSubstring "\0" $ BC.drop 2 string
          version = BC.unpack . fst . BC.breakSubstring "\0" $ BC.drop 12 string

dumpData :: Handle -> IO ()
dumpData handle = do
  dump <- hIsOpen handle
  if not dump then
      putStrLn "Got done message"
  else do
      putStrLn "Got 'done' message, dumping data . ."
      hSeek handle AbsoluteSeek 0
      samples <- B.unpack <$> BC.hGetContents handle
      withFile "/tmp/jyetech.ascii" WriteMode $
                   \out -> mapM_ (hPutStrLn out . show) samples
      putStrLn ". . done\n"

dumpFrameInfo :: Show a => a -> B.ByteString -> IO ()
dumpFrameInfo id payload =
  putStrLn $ "Read " ++ show (3 + B.length payload) ++ " bytes of frame "
               ++ show id ++ "/" ++ show (B.head payload) ++ "\n"


dumpMinMax :: Show a => String -> (a, a) -> IO ()
dumpMinMax label (max, min) =
    putStrLn $ label ++ " Max: " ++ show max ++ " Min: " ++ show min

getMinMax :: Integral a => Int -> [a] -> Int -> (Integer, Integer)
getMinMax len string offset = do
  case len of
    1 -> (getByte string offset, getByte string $ offset + 1)
    2 -> (getWord string offset, getWord string $ offset + 2)
    4 -> (getLong string offset, getLong string $ offset + 4)
    _ -> error "Oh shit we asked for an invalid length"

getByteMinMax, getWordMinMax, getSignedWordMinMax, getLongMinMax ::
    Integral a => [a] -> Int -> (Integer, Integer)
getByteMinMax = getMinMax 1
getWordMinMax = getMinMax 2
getSignedWordMinMax payload offset =
    (getSignedWord payload offset, getSignedWord payload $ offset + 2)
getLongMinMax = getMinMax 4

dumpValue :: Show a => String -> a -> IO ()
dumpValue name value = putStrLn $ name ++ " " ++ show value

getByte, getSignedByte :: Integral a => [a] -> Int -> Integer
getByte string offset = toInteger $ string !! offset
getSignedByte string offset = let val = getByte string offset
                              in if val >= 128 then val - 256 else val
    
getWord, getSignedWord :: Integral a => [a] -> Int -> Integer
getWord string offset =
    toInteger (string !! offset) + 256 * toInteger (string !! (offset + 1))
getSignedWord string offset = let val = getWord string offset
                              in if val >= 32768 then val - 65536 else val

getLong :: Integral a => [a] -> Int -> Integer
getLong string offset =
    toInteger (string !! offset)
    + 256 * (toInteger (string !! (offset + 1))
             + 256 * (toInteger (string !! (offset + 2))
                      + 256 * (toInteger (string !! (offset + 3)))))

runCommand :: SerialPort -> IO ()
runCommand port = do
  command <- getLine
  when (command == "quit" || command == "exit") $ exitWith ExitSuccess
  putStrLn $ "Running " ++ command
  let cmd = case command of
              "query"  -> "\254\224\4\0\0"
              "start"  -> "\254\225\4\0\192"
              "stop"   -> "\254\233\4\0\0"
              "conf"   -> "\254\192\4\0\32"
              "get"    -> "\254\192\4\0\33"
              "set"    -> "\254\192\4\0\34"
              "start m"-> "\254\192\7\0\36\0\49\0"
              "stop m" -> "\254\192\7\0\36\0\48\0"
              _        -> ""

  let len = B.length cmd
  when (len > 0) $ do
       putStrLn $ "Sending " ++ show len ++ " bytes."
       sent <- send port cmd
       flush port
       when (sent /= len) $ putStrLn ("Sent " ++ (show sent) ++ " bytes instead.")
       
