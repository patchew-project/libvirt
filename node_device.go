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
	Name       string       `xml:"name"`
	Path       string       `xml:"path,omitempty"`
	Parent     string       `xml:"parent,omitempty"`
	Driver     string       `xml:"driver>name,omitempty"`
	Capability interface{}	`xml:"capability"`
}

type NodeDeviceVendor struct {
	ID   	string	`xml:"id,attr,omitempty"`
	Name 	string	`xml:",chardata"`
}
type NodeDeviceProduct struct {
	ID   	string	`xml:"id,attr,omitempty"`
	Name	string	`xml:",chardata"`
}

type NodeDevicePciExpress struct {
	Links []NodeDevicePciExpressLink	`xml:"link"`
}

type NodeDevicePciExpressLink struct {
	Validity	string	`xml:"validity,attr,omitempty"`
	Speed    	float64	`xml:"speed,attr,omitempty"`
	Port     	int	`xml:"port,attr,omitempty"`
	Width    	int	`xml:"width,attr,omitempty"`
}

type NodeDeviceIOMMUGroup struct {
	Number	int	`xml:"number,attr"`
}

type NodeDeviceNUMA struct {
	Node	int	`xml:"node,attr"`
}

type NodeDevicePCIAddress struct {
	Domain   	string 	`xml:"domain,attr"`
	Bus      	string		`xml:"bus,attr"`
	Slot     	string 	`xml:"slot,attr"`
	Function	string 	`xml:"function,attr"`
}

type NodeDeviceSystemHardware struct {
	Vendor  string		`xml:"vendor"`
	Version string 		`xml:"version"`
	Serial  string 		`xml:"serial"`
	UUID    string 		`xml:"uuid"`
}

type NodeDeviceSystemFirmware struct {
	Vendor      	string 	`xml:"vendor"`
	Version     	string 	`xml:"version"`
	ReleaseDate	string 	`xml:"release_date"`
}

type NodeDeviceNetOffloadFeatures struct {
	Name	string	`xml:"name,attr"`
}

type NodeDeviceNetLink struct {
	State 	string 	`xml:"state,attr"`
	Speed	string 	`xml:"speed,attr,omitempty"`
}

type NodeDevicePciCapability struct {
	Domain       	int             		`xml:"domain,omitempty"`
	Bus          	int             		`xml:"bus,omitempty"`
	Slot         	int             		`xml:"slot,omitempty"`
	Function     	int             		`xml:"function,omitempty"`
	Product      	*NodeDeviceProduct          	`xml:"product,omitempty"`
	Vendor       	*NodeDeviceVendor          	`xml:"vendor,omitempty"`
	IommuGroup   	*NodeDeviceIOMMUGroup  		`xml:"iommuGroup,omitempty"`
	Numa         	*NodeDeviceNUMA            	`xml:"numa,omitempty"`
	PciExpress   	*NodeDevicePciExpress      	`xml:"pci-express,omitempty"`
	Capability	[]NodeDeviceNestedPciCapability	`xml:"capability,omitempty"`
}

type NodeDeviceNestedPciCapability struct {
	Type     	string       			`xml:"type,attr"`
	Address  	[]NodeDevicePCIAddress		`xml:"address,omitempty"`
	MaxCount	int          			`xml:"maxCount,attr,omitempty"`
}

type NodeDeviceSystemCapability struct {
	Product  	string         			`xml:"product"`
	Hardware	*NodeDeviceSystemHardware	`xml:"hardware"`
	Firmware	*NodeDeviceSystemFirmware	`xml:"firmware"`
}

type NodeDeviceUSBDeviceCapability struct {
	Bus     int   			`xml:"bus"`
	Device  int    			`xml:"device"`
	Product	*NodeDeviceProduct	`xml:"product,omitempty"`
	Vendor  *NodeDeviceVendor 	`xml:"vendor,omitempty"`
}

type NodeDeviceUSBCapability struct {
	Number      	int    	`xml:"number"`
	Class       	int    	`xml:"class"`
	Subclass    	int    	`xml:"subclass"`
	Protocol    	int    	`xml:"protocol"`
	Description	string	`xml:"description,omitempty"`
}

type NodeDeviceNetCapability struct {
	Interface  	string               		`xml:"interface"`
	Address    	string               		`xml:"address"`
	Link       	*NodeDeviceNetLink             	`xml:"link"`
	Features   	[]NodeDeviceNetOffloadFeatures	`xml:"feature,omitempty"`
	Capability	*NodeDeviceNestedNetCapability  `xml:"capability"`
}

type NodeDeviceNestedNetCapability struct {
	Type	string	`xml:"type,attr"`
}

type NodeDeviceSCSIHostCapability struct {
	Host       	int                			`xml:"host"`
	UniqueID   	int                			`xml:"unique_id"`
	Capability	[]NodeDeviceNestedSCSIHostCapability	`xml:"capability"`
}

type NodeDeviceNestedSCSIHostCapability struct {
	Type		string	`xml:"type,attr"`
	Vports    	int 	`xml:"vports,omitempty"`
	MaxVports	int 	`xml:"maxvports,,omitempty"`
	WWNN      	string 	`xml:"wwnn,omitempty"`
	WWPN      	string 	`xml:"wwpn,omitempty"`
	FabricWWN	string	`xml:"fabric_wwn,omitempty"`
}

type NodeDeviceSCSICapability struct {
	Host   int    `xml:"host"`
	Bus    int    `xml:"bus"`
	Target int    `xml:"target"`
	Lun    int    `xml:"lun"`
	Type   string `xml:"type"`
}

type NodeDeviceNestedStorageCapability struct {
	Type           string	`xml:"type,attr"`
	MediaAvailable int    	`xml:"media_available,omitempty"`
	MediaSize      int    	`xml:"media_size,omitempty"`
	MediaLable     int    	`xml:"media_label,omitempty"`
}

type NodeDeviceStorageCapability struct {
	Block        	string     				`xml:"block"`
	Bus          	string     				`xml:"bus"`
	DriverType   	string     				`xml:"drive_type"`
	Model        	string     				`xml:"model"`
	Vendor      	string     				`xml:"vendor"`
	Serial       	string     				`xml:"serial,omitempty"`
	Size         	int        				`xml:"size,omitempty"`
	Capability	*NodeDeviceNestedStorageCapability	`xml:"capability,omitempty"`
}

type NodeDeviceDRMCapability struct {
	Type	string	`xml:"type"`
}

func (c *NodeDevice) UnmarshalXML(d *xml.Decoder, start xml.StartElement) error {

	for {
		t, err := d.Token()
		if err != nil {
			return err
		}
		switch token := t.(type) {
		case xml.StartElement:
			switch token.Name.Local {
			case "name":
				var content string
				if err := d.DecodeElement(&content, &start); err != nil {
					return err
				}
				c.Name = content
			case "path":
				var content string
				if err := d.DecodeElement(&content, &start); err != nil {
					return err
				}
				c.Path = content
			case "parent":
				var content string
				if err := d.DecodeElement(&content, &start); err != nil {
					return err
				}
				c.Parent = content
			case "driver":
				tmp := struct {
					Name string `xml:"name"`
				}{}
				if err := d.DecodeElement(&tmp, &token); err != nil {
					return err
				}
				c.Driver = tmp.Name
			case "capability":
				for _, attr := range token.Attr {
					if attr.Name.Local == "type" {
						switch attr.Value {
						case "pci":
							var pciCaps NodeDevicePciCapability
							if err := d.DecodeElement(&pciCaps, &token); err != nil {
								return err
							}
							c.Capability = pciCaps
						case "system":
							var systemCaps NodeDeviceSystemCapability
							if err := d.DecodeElement(&systemCaps, &token); err != nil {
								return err
							}
							c.Capability = systemCaps
						case "usb_device":
							var usbdevCaps NodeDeviceUSBDeviceCapability
							if err := d.DecodeElement(&usbdevCaps, &token); err != nil {
								return err
							}
							c.Capability = usbdevCaps
						case "usb":
							var usbCaps NodeDeviceUSBCapability
							if err := d.DecodeElement(&usbCaps, &token); err != nil {
								return err
							}
							c.Capability = usbCaps
						case "net":
							var netCaps NodeDeviceNetCapability
							if err := d.DecodeElement(&netCaps, &token); err != nil {
								return err
							}
							c.Capability = netCaps
						case "scsi_host":
							var scsiHostCaps NodeDeviceSCSIHostCapability
							if err := d.DecodeElement(&scsiHostCaps, &token); err != nil {
								return err
							}
							c.Capability = scsiHostCaps
						case "scsi":
							var scsiCaps NodeDeviceSCSICapability
							if err := d.DecodeElement(&scsiCaps, &token); err != nil {
								return err
							}
							c.Capability = scsiCaps
						case "storage":
							var storageCaps NodeDeviceStorageCapability
							if err := d.DecodeElement(&storageCaps, &token); err != nil {
								return err
							}
							c.Capability = storageCaps
						case "drm":
							var drmCaps NodeDeviceDRMCapability
							if err := d.DecodeElement(&drmCaps, &token); err != nil {
								return err
							}
							c.Capability = drmCaps
						}
					}
				}
			}
		case xml.EndElement:
			return nil
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
