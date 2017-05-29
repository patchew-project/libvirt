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
	"encoding/xml"
	"fmt"
)

type NodeDevice struct {
	Name       string          `xml:"name"`
	Path       string          `xml:"path,omitempty"`
	Parent     string          `xml:"parent,omitempty"`
	Driver     string          `xml:"driver>name,omitempty"`
	Capability *CapabilityType `xml:"capability"`
}

type CapabilityType struct {
	Type interface{} `xml:"type,attr"`
}

type IDName struct {
	ID   string `xml:"id,attr"`
	Name string `xml:",chardata"`
}

type PciExpress struct {
	Links []PciExpressLink `xml:"link"`
}

type PciExpressLink struct {
	Validity string  `xml:"validity,attr,omitempty"`
	Speed    float64 `xml:"speed,attr,omitempty"`
	Port     int     `xml:"port,attr,omitempty"`
	Width    int     `xml:"width,attr,omitempty"`
}

type IOMMUGroupType struct {
	Number int `xml:"number,attr"`
}

type NUMA struct {
	Node int `xml:"node,attr"`
}

type PciCapabilityType struct {
	Domain       int             `xml:"domain,omitempty"`
	Bus          int             `xml:"bus,omitempty"`
	Slot         int             `xml:"slot,omitempty"`
	Function     int             `xml:"function,omitempty"`
	Product      IDName          `xml:"product,omitempty"`
	Vendor       IDName          `xml:"vendor,omitempty"`
	IommuGroup   IOMMUGroupType  `xml:"iommuGroup,omitempty"`
	Numa         NUMA            `xml:"numa,omitempty"`
	PciExpress   PciExpress      `xml:"pci-express,omitempty"`
	Capabilities []PciCapability `xml:"capability,omitempty"`
}

type PCIAddress struct {
	Domain   string `xml:"domain,attr"`
	Bus      string `xml:"bus,attr"`
	Slot     string `xml:"slot,attr"`
	Function string `xml:"function,attr"`
}

type PciCapability struct {
	Type     string       `xml:"type,attr"`
	Address  []PCIAddress `xml:"address,omitempty"`
	MaxCount int          `xml:"maxCount,attr,omitempty"`
}

type SystemHardware struct {
	Vendor  string `xml:"vendor"`
	Version string `xml:"version"`
	Serial  string `xml:"serial"`
	UUID    string `xml:"uuid"`
}

type SystemFirmware struct {
	Vendor      string `xml:"vendor"`
	Version     string `xml:"version"`
	ReleaseData string `xml:"release_date"`
}

type SystemCapabilityType struct {
	Product  string         `xml:"product"`
	Hardware SystemHardware `xml:"hardware"`
	Firmware SystemFirmware `xml:"firmware"`
}

type USBDeviceCapabilityType struct {
	Bus     int    `xml:"bus"`
	Device  int    `xml:"device"`
	Product IDName `xml:"product,omitempty"`
	Vendor  IDName `xml:"vendor,omitempty"`
}

type USBCapabilityType struct {
	Number      int    `xml:"number"`
	Class       int    `xml:"class"`
	Subclass    int    `xml:"subclass"`
	Protocol    int    `xml:"protocol"`
	Description string `xml:"description,omitempty"`
}

type NetOffloadFeatures struct {
	Name string `xml:"number"`
}

type NetLink struct {
	State string `xml:"state,attr"`
	Speed string `xml:"speed,attr,omitempty"`
}

type NetCapability struct {
	Type string `xml:"type,attr"`
}

type NetCapabilityType struct {
	Interface  string               `xml:"interface"`
	Address    string               `xml:"address"`
	Link       NetLink              `xml:"link"`
	Features   []NetOffloadFeatures `xml:"feature,omitempty"`
	Capability NetCapability        `xml:"capability"`
}

type SCSIVportsOPS struct {
	Vports    int `xml:"vports,omitempty"`
	MaxVports int `xml:"maxvports,,omitempty"`
}

type SCSIFCHost struct {
	WWNN      string `xml:"wwnn,omitempty"`
	WWPN      string `xml:"wwpn,omitempty"`
	FabricWWN string `xml:"fabric_wwn,omitempty"`
}

type SCSIHostCapability struct {
	VportsOPS SCSIVportsOPS `xml:"vports_ops"`
	FCHost    SCSIFCHost    `xml:"fc_host"`
}

type SCSIHostCapabilityType struct {
	Host       int                `xml:"host"`
	UniqueID   int                `xml:"unique_id"`
	Capability SCSIHostCapability `xml:"capability"`
}

type SCSICapabilityType struct {
	Host   int    `xml:"host"`
	Bus    int    `xml:"bus"`
	Target int    `xml:"target"`
	Lun    int    `xml:"lun"`
	Type   string `xml:"type"`
}

type StroageCap struct {
	Type           string `xml:"match,attr"`
	MediaAvailable int    `xml:"media_available,omitempty"`
	MediaSize      int    `xml:"media_size,omitempty"`
	MediaLable     int    `xml:"media_label,omitempty"`
}

type StorageCapabilityType struct {
	Block        string     `xml:"block"`
	Bus          string     `xml:"bus"`
	DriverType   string     `xml:"drive_type"`
	Model        string     `xml:"model"`
	Vendor       string     `xml:"vendor"`
	Serial       string     `xml:"serial"`
	Size         int        `xml:"size"`
	Capatibility StroageCap `xml:"capability,omitempty"`
}

type DRMCapabilityType struct {
	Type string `xml:"type"`
}

func (c *CapabilityType) UnmarshalXML(d *xml.Decoder, start xml.StartElement) error {
	for _, attr := range start.Attr {
		fmt.Println(attr.Name.Local)
		if attr.Name.Local == "type" {
			switch attr.Value {
			case "pci":
				var pciCaps PciCapabilityType
				if err := d.DecodeElement(&pciCaps, &start); err != nil {
					return err
				}
				c.Type = pciCaps
			case "system":
				var systemCaps SystemCapabilityType
				if err := d.DecodeElement(&systemCaps, &start); err != nil {
					return err
				}
				c.Type = systemCaps
			case "usb_device":
				var usbdevCaps USBDeviceCapabilityType
				if err := d.DecodeElement(&usbdevCaps, &start); err != nil {
					return err
				}
				c.Type = usbdevCaps
			case "usb":
				var usbCaps USBCapabilityType
				if err := d.DecodeElement(&usbCaps, &start); err != nil {
					return err
				}
				c.Type = usbCaps
			case "net":
				var netCaps NetCapabilityType
				if err := d.DecodeElement(&netCaps, &start); err != nil {
					return err
				}
				c.Type = netCaps
			case "scsi_host":
				var scsiHostCaps SCSIHostCapabilityType
				if err := d.DecodeElement(&scsiHostCaps, &start); err != nil {
					return err
				}
				c.Type = scsiHostCaps
			case "scsi":
				var scsiCaps SCSICapabilityType
				if err := d.DecodeElement(&scsiCaps, &start); err != nil {
					return err
				}
				c.Type = scsiCaps
			case "storage":
				var storageCaps StorageCapabilityType
				if err := d.DecodeElement(&storageCaps, &start); err != nil {
					return err
				}
				c.Type = storageCaps
			case "drm":
				var drmCaps DRMCapabilityType
				if err := d.DecodeElement(&drmCaps, &start); err != nil {
					return err
				}
				c.Type = drmCaps
			}
		}
	}
	return nil
}

func (c *NodeDevice) Unmarshal(doc string) error {
	return xml.Unmarshal([]byte(doc), c)
}

func (c *NodeDevice) Marshal() (string, error) {
	doc, err := xml.MarshalIndent(c, "", "  ")
	if err != nil {
		return "", err
	}
	return string(doc), nil
}
