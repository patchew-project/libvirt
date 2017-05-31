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
)

type NodeDevice struct {
	Name       string                 `xml:"name"`
	Path       string                 `xml:"path,omitempty"`
	Parent     string                 `xml:"parent,omitempty"`
	Driver     string                 `xml:"driver>name,omitempty"`
	Capability []NodeDeviceCapability `xml:"capability"`
}

type NodeDeviceCapability struct {
	Domain      int                            `xml:"domain,omitempty"`
	Bus         int                            `xml:"bus,omitempty"`
	Slot        int                            `xml:"slot,omitempty"`
	Function    int                            `xml:"function,omitempty"`
	IommuGroup  *NodeDeviceIOMMUGroup          `xml:"iommuGroup,omitempty"`
	Numa        *NodeDeviceNUMA                `xml:"numa,omitempty"`
	PciExpress  *NodeDevicePciExpress          `xml:"pci-express,omitempty"`
	Hardware    *NodeDeviceSystemHardware      `xml:"hardware"`
	Firmware    *NodeDeviceSystemFirmware      `xml:"firmware"`
	Device      int                            `xml:"device"`
	Number      int                            `xml:"number"`
	Class       int                            `xml:"class"`
	Subclass    int                            `xml:"subclass"`
	Protocol    int                            `xml:"protocol"`
	Description string                         `xml:"description,omitempty"`
	Interface   string                         `xml:"interface"`
	Address     string                         `xml:"address"`
	Link        *NodeDeviceNetLink             `xml:"link"`
	Features    []NodeDeviceNetOffloadFeatures `xml:"feature,omitempty"`
	UniqueID    int                            `xml:"unique_id"`
	Target      int                            `xml:"target"`
	Lun         int                            `xml:"lun"`
	Block       string                         `xml:"block"`
	DriverType  string                         `xml:"drive_type"`
	Model       string                         `xml:"model"`
	Serial      string                         `xml:"serial"`
	Size        int                            `xml:"size"`
	Host        int                            `xml:"host"`
	Type        string                         `xml:"type"`
	Product     *NodeDeviceProduct             `xml:"product,omitempty"`
	Vendor      *NodeDeviceVendor              `xml:"vendor,omitempty"`
	Capability  []NodeDeviceNestedCapabilities `xml:"capability,omitempty"`
}

type NodeDeviceVendor struct {
	ID   string `xml:"id,attr,omitempty"`
	Name string `xml:",chardata"`
}
type NodeDeviceProduct struct {
	ID   string `xml:"id,attr,omitempty"`
	Name string `xml:",chardata"`
}

type NodeDeviceNestedCapabilities struct {
	Type           string                   `xml:"type,attr"`
	Address        []NodeDevicePCIAddress   `xml:"address,omitempty"`
	MaxCount       int                      `xml:"maxCount,attr,omitempty"`
	VportsOPS      *NodeDeviceSCSIVportsOPS `xml:"vports_ops,omitempty"`
	FCHost         *NodeDeviceSCSIFCHost    `xml:"fc_host,omitempty"`
	MediaAvailable int                      `xml:"media_available,omitempty"`
	MediaSize      int                      `xml:"media_size,omitempty"`
	MediaLable     int                      `xml:"media_label,omitempty"`
}

type NodeDevicePciExpress struct {
	Links []NodeDevicePciExpressLink `xml:"link"`
}

type NodeDevicePciExpressLink struct {
	Validity string  `xml:"validity,attr,omitempty"`
	Speed    float64 `xml:"speed,attr,omitempty"`
	Port     int     `xml:"port,attr,omitempty"`
	Width    int     `xml:"width,attr,omitempty"`
}

type NodeDeviceIOMMUGroup struct {
	Number int `xml:"number,attr"`
}

type NodeDeviceNUMA struct {
	Node int `xml:"node,attr"`
}

type NodeDevicePCIAddress struct {
	Domain   string `xml:"domain,attr"`
	Bus      string `xml:"bus,attr"`
	Slot     string `xml:"slot,attr"`
	Function string `xml:"function,attr"`
}

type NodeDeviceSystemHardware struct {
	Vendor  string `xml:"vendor"`
	Version string `xml:"version"`
	Serial  string `xml:"serial"`
	UUID    string `xml:"uuid"`
}

type NodeDeviceSystemFirmware struct {
	Vendor      string `xml:"vendor"`
	Version     string `xml:"version"`
	ReleaseData string `xml:"release_date"`
}

type NodeDeviceNetOffloadFeatures struct {
	Name string `xml:"number"`
}

type NodeDeviceNetLink struct {
	State string `xml:"state,attr"`
	Speed string `xml:"speed,attr,omitempty"`
}

type NetCapability struct {
	Type string `xml:"type,attr"`
}

type NodeDeviceSCSIVportsOPS struct {
	Vports    int `xml:"vports,omitempty"`
	MaxVports int `xml:"maxvports,,omitempty"`
}

type NodeDeviceSCSIFCHost struct {
	WWNN      string `xml:"wwnn,omitempty"`
	WWPN      string `xml:"wwpn,omitempty"`
	FabricWWN string `xml:"fabric_wwn,omitempty"`
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
