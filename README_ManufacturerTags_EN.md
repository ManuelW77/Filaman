# Manufacturer Tags - English Documentation

## Overview

The FilaMan NFC system supports **Manufacturer Tags** that allow filament producers to create standardized NFC tags for their products. When scanned, these tags automatically create the necessary entries in Spoolman (brand, filament type, and spool) without requiring manual setup.

## How Manufacturer Tags Work

### Process Flow

1. **Tag Detection**: When a tag without `sm_id` is scanned, the system checks for manufacturer tag format
2. **Brand Creation/Lookup**: The system searches for the brand in Spoolman or creates it if it doesn't exist
3. **Filament Type Creation/Lookup**: The filament type is created or found based on brand, material, and specifications
4. **Spool Creation**: A new spool entry is automatically created with the tag's UID as reference
5. **Tag Update**: The tag is updated with the new Spoolman spool ID (`sm_id`)

### Why Use Manufacturer Tags?

- **Automatic Integration**: No manual data entry required
- **Standardized Format**: Consistent product information across different manufacturers
- **Inventory Management**: Automatic creation of complete Spoolman entries
- **Traceability**: Direct link between physical product and digital inventory

## Tag Format Specification

### JSON Structure

Manufacturer tags must contain a JSON payload with specific fields using **short keys** to minimize tag size:

```json
{
    "b": "Brand/Vendor Name",
    "an": "Article Number",
    "t": "Filament Type (PLA, PETG, etc)",
    "c": "Filament Color without # (FF5733)",
    "mc": "Optional Multicolor Filament Colors without # (FF0000,00FF00,0000FF)",
    "mcd": "Optional Multicolor Direction as Word (coaxial, longitudinal)",
    "cn": "Color Name (red, Blueberry, Arctic Blue)",
    "et": "Extruder Temp as Number in C° (230)",
    "bt": "Bed Temp as Number in C° (60)",
    "di": "Diameter as Float (1.75)",
    "de": "Density as Float (1.24)",
    "sw": "Empty Spool Weight as Number in g (180)",
    "u": "URL to get the Filament with the Article Number"
}
```

### Required Fields

- **`b`** (brand): Manufacturer/brand name
- **`an`** (article number): Unique product identifier
- **`t`** (type): Material type (PLA, PETG, ABS, etc.)
- **`c`** (color): Hex color code without #
- **`cn`** (color name): Human-readable color name
- **`et`** (extruder temp): Recommended extruder temperature in Celsius
- **`bt`** (bed temp): Recommended bed temperature in Celsius
- **`di`** (diameter): Filament diameter in mm
- **`de`** (density): Material density in g/cm³
- **`sw`** (spool weight): Empty spool weight in grams

### Optional Fields

- **`mc`** (multicolor): Comma-separated hex colors for multicolor filaments
- **`mcd`** (multicolor direction): Direction for multicolor (coaxial, longitudinal)
- **`u`** (url): Product URL with direct link to the article e.g. for reordering

### Example Tag

```json
{"b":"Recycling Fabrik","an":"FX1_PETG-S175-1000-DAEM00055","t":"PETG","c":"FF5733","cn":"Vibrant Orange","et":"230","bt":"70","di":"1.75","de":"1.24","sw":"180","u":"https://www.recyclingfabrik.com/search?q="}
```

## Implementation Guidelines

### For Manufacturers

1. **Tag Encoding**: Use NDEF format with MIME type `application/json`
2. **Data Minimization**: Use the compact JSON format to fit within tag size limits
3. **Quality Control**: Ensure all required fields are present and correctly formatted
4. **Testing**: Verify tags work with FilaMan system before production

### Tag Size Considerations

- **NTAG213**: 144 bytes user data (suitable for basic tags)
- **NTAG215**: 504 bytes user data (recommended for comprehensive data)
- **NTAG216**: 888 bytes user data (maximum compatibility)

### Best Practices

- Keep brand names consistent across all products
- Use standardized material type names (PLA, PETG, ABS, etc.)
- Provide accurate temperature recommendations
- Include meaningful color names for user experience
- Test tags with the FilaMan system before mass production

## System Integration

### Spoolman Database Structure

When a manufacturer tag is processed, the system creates:

1. **Vendor Entry**: Brand information in Spoolman vendor database
2. **Filament Entry**: Material specifications and properties
3. **Spool Entry**: Individual spool with weight and NFC tag reference

### Fast-Path Recognition

Once a tag is processed and updated with `sm_id`, it uses the fast-path recognition system for quick subsequent scans.

## Troubleshooting

### Common Issues

- **Tag Too Small**: Use NTAG215 or NTAG216 for larger JSON payloads
- **Missing Fields**: Ensure all required fields are present
- **Invalid Format**: Verify JSON syntax and field types
- **Spoolman Connection**: Ensure FilaMan can connect to Spoolman API

### Validation

The system validates:
- JSON format correctness
- Required field presence
- Data type compliance
- Tag size compatibility

## Technical Details

### Processing Algorithm

1. Tag scan detects no `sm_id` field
2. System checks for `b` (brand) and `an` (article number) fields
3. `checkVendor()` creates or finds brand in Spoolman
4. `checkFilament()` creates or finds filament type
5. `createSpool()` creates new spool entry
6. Tag is updated with new `sm_id`

### Error Handling

- Graceful fallback for network issues
- Detailed logging for debugging
- User feedback for failed operations
- Retry mechanisms for temporary failures

This system enables seamless integration of manufacturer filament products into the FilaMan ecosystem while maintaining data consistency and user experience.