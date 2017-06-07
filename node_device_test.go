/*
 * This file is part of the libvirt-go-xml project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 */

package libvirtxml

import (
	"reflect"
	"strings"
	"testing"
)

var NodeDeviceTestData = []struct {
	Object *NodeDevice
	XML    []string
}{
	{
		Object: &NodeDevice{
			Name:   "pci_0000_81_00_0",
			Parent: "pci_0000_80_01_0",
			Driver: "ixgbe",
			Capability: NodeDevicePciCapability{
				Domain:   1,
				Bus:      21,
				Slot:     10,
				Function: 50,
				Product: &NodeDeviceProduct{
					ID:   "0x1528",
					Name: "Ethernet Controller 10-Gigabit X540-AT2",
				},
				Vendor: &NodeDeviceVendor{
					ID:   "0x8086",
					Name: "Intel Corporation",
				},
				IommuGroup: &NodeDeviceIOMMUGroup{
					Number: 3,
				},
				Numa: &NodeDeviceNUMA{
					Node: 1,
				},
				Capability: []NodeDeviceNestedPciCapability{
					NodeDeviceNestedPciCapability{
						Type: "virt_functions",
						Address: []NodeDevicePCIAddress{
							NodeDevicePCIAddress{
								Domain:   "0x0000",
								Bus:      "0x81",
								Slot:     "0x10",
								Function: "0x1",
							},
							NodeDevicePCIAddress{
								Domain:   "0x0000",
								Bus:      "0x81",
								Slot:     "0x10",
								Function: "0x3",
							},
						},
						MaxCount: 63,
					},
				},
			},
		},
		XML: []string{
			`<device>`,
			`  <name>pci_0000_81_00_0</name>`,
			`  <parent>pci_0000_80_01_0</parent>`,
			`  <driver>`,
			`		<name>ixgbe</name>`,
			`  </driver>`,
			`  <capability type='pci'>`,
			`	<domain>1</domain>`,
			`	<bus>21</bus>`,
			`	<slot>10</slot>`,
			`	<function>50</function>`,
			`	<product id='0x1528'>Ethernet Controller 10-Gigabit X540-AT2</product>`,
			`	<vendor id='0x8086'>Intel Corporation</vendor>`,
			`	<capability type='virt_functions' maxCount='63'>`,
			`	  <address domain='0x0000' bus='0x81' slot='0x10' function='0x1'/>`,
			`	  <address domain='0x0000' bus='0x81' slot='0x10' function='0x3'/>`,
			`	</capability>`,
			`	<iommuGroup number='3'>`,
			`	  <address domain='0x0000' bus='0x15' slot='0x00' function='0x4'/>`,
			`	</iommuGroup>`,
			`   <numa node='1'/>`,
			`  </capability>`,
			`</device>`,
		},
	},
	{
		Object: &NodeDevice{
			Name:   "pci_10df_fe00_0_scsi_host",
			Parent: "pci_10df_fe00_0",
			Capability: NodeDeviceSCSIHostCapability{
				Host: 4,
				Capability: []NodeDeviceNestedSCSIHostCapability{
					NodeDeviceNestedSCSIHostCapability{
						Type: "fc_host",
						WWNN: "20000000c9848141",
						WWPN: "10000000c9848141",
					},
					NodeDeviceNestedSCSIHostCapability{
						Type: "vport_ops",
					},
				},
			},
		},
		XML: []string{
			`<device>`,
			`	<name>pci_10df_fe00_0_scsi_host</name>`,
			`	<parent>pci_10df_fe00_0</parent>`,
			`	<capability type='scsi_host'>`,
			`		<host>4</host>`,
			`		<capability type='fc_host'>`,
			`			<wwnn>20000000c9848141</wwnn>`,
			`			<wwpn>10000000c9848141</wwpn>`,
			`		</capability>`,
			`		<capability type='vport_ops' />`,
			`	</capability>`,
			`</device>`,
		},
	},
	{
		Object: &NodeDevice{
			Name:   "computer",
			Capability: NodeDeviceSystemCapability{
				Hardware: &NodeDeviceSystemHardware{
					Vendor:	"LENOVO",
					Version: "ThinkPad T61",
					Serial: "L3B2616",
					UUID: "97e80381-494f-11cb-8e0e-cbc168f7d753",
				},
				Firmware: &NodeDeviceSystemFirmware{
					Vendor: "LENOVO",
					Version: "7LET51WW (1.21 )",
					ReleaseDate: "08/22/2007",
				},
			},
		},
		XML: []string{
			`<device>`,
			`  <name>computer</name>`,
			`  <capability type='system'>`,
			`    <hardware>`,
			`      <vendor>LENOVO</vendor>`,
			`      <version>ThinkPad T61</version>`,
			`      <serial>L3B2616</serial>`,
			`      <uuid>97e80381-494f-11cb-8e0e-cbc168f7d753</uuid>`,
			`    </hardware>`,
			`    <firmware>`,
			`      <vendor>LENOVO</vendor>`,
			`      <version>7LET51WW (1.21 )</version>`,
			`      <release_date>08/22/2007</release_date>`,
			`    </firmware>`,
			`  </capability>`,
			`</device>`,
		},
	},
	{
		Object: &NodeDevice{
			Name:   "usb_device_1d6b_1_0000_00_1a_0",
			Parent: "pci_8086_2834",
			Driver: "usb",
			Capability: NodeDeviceUSBDeviceCapability{
				Bus: 3,
				Device: 1,
				Product: &NodeDeviceProduct{
					ID: "0x0001",
					Name: "1.1 root hub",
				},
				Vendor: &NodeDeviceVendor{
					ID: "0x1d6b",
					Name: "Linux Foundation",
				},
			},
		},
		XML: []string{
			`<device>`,
			`  <name>usb_device_1d6b_1_0000_00_1a_0</name>`,
			`  <parent>pci_8086_2834</parent>`,
			`  <driver>`,
			`    <name>usb</name>`,
			`  </driver>`,
			`  <capability type='usb_device'>`,
			`    <bus>3</bus>`,
			`    <device>1</device>`,
			`    <product id='0x0001'>1.1 root hub</product>`,
			`    <vendor id='0x1d6b'>Linux Foundation</vendor>`,
			`  </capability>`,
			`</device>`,
		},
	},
	{
		Object: &NodeDevice{
			Name:   "usb_device_1d6b_1_0000_00_1a_0_if0",
			Parent: "usb_device_1d6b_1_0000_00_1a_0",
			Driver: "hub",
			Capability: NodeDeviceUSBCapability{
				Number: 0,
				Class: 9,
				Subclass:0,
				Protocol: 0,
			},
		},
		XML: []string{
			`<device>`,
			`  <name>usb_device_1d6b_1_0000_00_1a_0_if0</name>`,
			`  <parent>usb_device_1d6b_1_0000_00_1a_0</parent>`,
			`  <driver>`,
			`    <name>hub</name>`,
			`  </driver>`,
			`  <capability type='usb'>`,
			`    <number>0</number>`,
			`    <class>9</class>`,
			`    <subclass>0</subclass>`,
			`    <protocol>0</protocol>`,
			`  </capability>`,
			`</device>`,
		},
	},
	{
		Object: &NodeDevice{
			Name:   "net_wlp3s0_4c_eb_42_aa_aa_82",
			Path: "/sys/devices/pci0000:00/0000:00:1c.1/0000:03:00.0/net/wlp3s0",
			Parent: "pci_0000_03_00_0",
			Capability: NodeDeviceNetCapability{
				Interface: "wlp3s0",
				Address: "4c:eb:42:aa:aa:82",
				Link: &NodeDeviceNetLink{
					State: "up",
				},
				Features: []NodeDeviceNetOffloadFeatures{
					NodeDeviceNetOffloadFeatures{
						Name: "sg",
					},
					NodeDeviceNetOffloadFeatures{
						Name: "gso",
					},
					NodeDeviceNetOffloadFeatures{
						Name: "gro",
					},
				},
				Capability: &NodeDeviceNestedNetCapability{
					Type: "80211",
				},
			},
		},
		XML: []string{
			`<device>`,
			`  <name>net_wlp3s0_4c_eb_42_aa_aa_82</name>`,
			`  <path>/sys/devices/pci0000:00/0000:00:1c.1/0000:03:00.0/net/wlp3s0</path>`,
			`  <parent>pci_0000_03_00_0</parent>`,
			`  <capability type='net'>`,
			`    <interface>wlp3s0</interface>`,
			`    <address>4c:eb:42:aa:aa:82</address>`,
			`    <link state='up'/>`,
			`    <feature name='sg'/>`,
			`    <feature name='gso'/>`,
			`    <feature name='gro'/>`,
			`    <capability type='80211'/>`,
			`  </capability>`,
			`</device>`,
		},
	},
	{
		Object: &NodeDevice{
			Name: "storage_model_DVDRAM_GSA_U10N",
			Parent: "pci_8086_2850_scsi_host_scsi_device_lun0",
			Capability: NodeDeviceStorageCapability{
				Block: "/dev/sr1",
				Bus: "pci",
				DriverType: "cdrom",
				Model:"DVDRAM GSA-U10N",
				Vendor: "HL-DT-ST",
				Capability: &NodeDeviceNestedStorageCapability{
					Type: "removable",
					MediaAvailable: 0,
					MediaSize: 0,
				},
			},
		},
		XML: []string{
			`<device>`,
			`  <name>storage_model_DVDRAM_GSA_U10N</name>`,
			`  <parent>pci_8086_2850_scsi_host_scsi_device_lun0</parent>`,
			`  <capability type='storage'>`,
			`    <block>/dev/sr1</block>`,
			`    <bus>pci</bus>`,
			`    <drive_type>cdrom</drive_type>`,
			`    <model>DVDRAM GSA-U10N</model>`,
			`    <vendor>HL-DT-ST</vendor>`,
			`    <capability type='removable'>`,
			`      <media_available>0</media_available>`,
			`      <media_size>0</media_size>`,
			`    </capability>`,
			`  </capability>`,
			`</device>`,
		},
	},

}

func TestNodeDevice(t *testing.T) {
	for _, test := range NodeDeviceTestData {
		xmlDoc := strings.Join(test.XML, "\n")
		nodeDevice := NodeDevice{}
		err := nodeDevice.Unmarshal(xmlDoc)
		if err != nil {
			t.Fatal(err)
		}

		res := reflect.DeepEqual(&nodeDevice, test.Object)
		if !res {
			t.Fatal("Bad NodeDevice object creation.", "\nExpected: ", test.Object, "\nActual: ", nodeDevice)
		}
	}
}
