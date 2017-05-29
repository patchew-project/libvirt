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
			Capability: &CapabilityType{
				Type: PciCapabilityType{
					Domain:   1,
					Bus:      21,
					Slot:     10,
					Function: 50,
					Product: IDName{
						ID:   "0x1528",
						Name: "Ethernet Controller 10-Gigabit X540-AT2",
					},
					Vendor: IDName{
						ID:   "0x8086",
						Name: "Intel Corporation",
					},
					IommuGroup: IOMMUGroupType{
						Number: 3,
					},
					Numa: NUMA{
						Node: 1,
					},
					Capabilities: []PciCapability{
						PciCapability{
							Type:     "virt_functions",
							MaxCount: 63,
						},
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
			`	<capability type='virt_functions' maxCount='63'/>`,
			`	<iommuGroup number='3'>`,
			`	  <address domain='0x0000' bus='0x15' slot='0x00' function='0x4'/>`,
			`	</iommuGroup>`,
			`   <numa node='1'/>`,
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
			t.Fatal("Bad NodeDevice object creation.")
		}
	}
}
