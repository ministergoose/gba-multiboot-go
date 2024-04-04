package main

import (
	"encoding/binary"
	"errors"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/vbauerster/mpb/v8"
	"github.com/vbauerster/mpb/v8/decor"
	"go.bug.st/serial"
)

type State int

const (
	State_Init State = iota
	State_SendSize
	State_SendHeader
	State_SendData
)

const (
	cmd_startup   uint8 = 0x01
	cmd_writedone uint8 = 0x02
	cmd_exit      uint8 = 0x03
)

func ReadCom(port serial.Port, ch chan byte) {
	buff := make([]byte, 512)
	var sb strings.Builder
	var bar *mpb.Bar

	p := mpb.New()
	initBar := false

	for {
		n, err := port.Read(buff)
		if err != nil {
			log.Fatal(err)
		}
		if n == 0 {
			log.Fatal(errors.New("EOF"))
		}

		for i := 0; i < n; i++ {
			c := buff[i]

			if c < 9 {
				ch <- c
			} else {
				sb.WriteByte(c)
				if c == 0x0A {
					str := sb.String()
					if strings.Contains(str, "./.") {
						parts := strings.Split(str, "./.")

						current, err := strconv.ParseInt(parts[0], 10, 64)
						if err != nil {
							log.Fatal(err)
						}

						if !initBar {
							max, err := strconv.ParseInt(strings.TrimRight(parts[1], "\r\n"), 10, 64)
							if err != nil {
								log.Fatal(err)
							}

							bar = p.New(max,
								mpb.BarStyle().Lbound("╢").Filler("▌").Tip("▌").Padding("░").Rbound("╟"),
								mpb.PrependDecorators(
									decor.Name("Send rom:", decor.WC{C: decor.DindentRight | decor.DextraSpace}),
									decor.CountersNoUnit("%d / %d", decor.WCSyncWidth, decor.WC{C: decor.DindentRight | decor.DextraSpace}),
									decor.OnComplete(decor.AverageETA(decor.ET_STYLE_GO), "done"),
								),
								mpb.AppendDecorators(decor.Percentage()),
							)
							initBar = true
						}

						bar.SetCurrent(current)
						sb.Reset()
					} else {
						fmt.Print(sb.String())
						sb.Reset()
					}
				}
			}
		}
	}
}

func WriteCom(port serial.Port, buff []byte) {
	_, err := port.Write(buff)
	if err != nil {
		log.Fatal(err)
	}
	port.Drain()
}

func parseArgs() (string, string, int) {
	var err error
	filePath := ""
	comPort := "COM3"
	baudRate := 57600

	if len(os.Args) == 2 {
		filePath = os.Args[1]
	} else if len(os.Args) == 3 {
		comPort = os.Args[1]
		filePath = os.Args[2]
	} else if len(os.Args) == 3 {
		comPort = os.Args[1]
		baudRate, err = strconv.Atoi(os.Args[2])
		if err != nil {
			fmt.Fprintf(os.Stderr, "Usage: %s [COM port (Default: \"COM3\")] [Baud Rate (Default: 57600)] <file path>", filepath.Base(os.Args[0]))
			os.Exit(1)
		}
		filePath = os.Args[3]
	} else {
		fmt.Fprintf(os.Stderr, "Usage: %s [COM port (Default: \"COM3\")] [Baud Rate (Default: 57600)] <file path>\n", filepath.Base(os.Args[0]))
		os.Exit(1)
	}

	return filePath, comPort, baudRate
}

func main() {

	filePath, comPort, baudRate := parseArgs()

	port, err := serial.Open(comPort, &serial.Mode{BaudRate: baudRate})
	if err != nil {
		log.Fatal(err)
	}
	defer port.Close()

	data, err := os.ReadFile(filePath)
	if err != nil {
		log.Fatal(err)
	}

	fsize := len(data)

	if fsize%16 != 0 {
		data = append(data, make([]byte, (16-(fsize%16)))...)
		fsize = len(data)
	}

	if fsize > 0x40000 {
		log.Fatal(errors.New("max file size 256kB"))
	}

	startIdx := 0
	state := State_Init
	statech := make(chan byte)
	defer close(statech)

	go ReadCom(port, statech)

done:
	for {
		switch state {
		case State_Init:
			s := <-statech
			if s == cmd_startup {
				state = State_SendSize
			}
		case State_SendSize:
			temp32 := make([]byte, 4)
			binary.LittleEndian.PutUint32(temp32, uint32(fsize))
			WriteCom(port, temp32)

			state = State_SendHeader
		case State_SendHeader:
			WriteCom(port, data[:0xc0])
			startIdx = 0xc0

			state = State_SendData
		case State_SendData:
			s := <-statech
			if s == cmd_exit {
				break done
			}
			if s == cmd_writedone {
				endIdx := startIdx + 32
				if endIdx > fsize {
					endIdx = fsize
				}

				WriteCom(port, data[startIdx:endIdx])

				startIdx = endIdx
			}
		}
	}
}
