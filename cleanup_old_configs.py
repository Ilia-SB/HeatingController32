#!/usr/bin/env python3
"""
Cleanup script to remove old item*.cfg files after successful migration
to the new per-property format.

This script should only be run AFTER verifying that the new per-property
files work correctly with the updated firmware.
"""

import os
import sys
from pathlib import Path

def main():
    """Main cleanup function"""
    # Determine data directory
    if len(sys.argv) > 1:
        data_dir = Path(sys.argv[1])
    else:
        # Default to current directory/data
        data_dir = Path("data")
    
    if not data_dir.exists():
        print(f"Data directory not found: {data_dir}")
        return 1
    
    # Find all item*.cfg files
    item_files = list(data_dir.glob("item*.cfg"))
    if not item_files:
        print("No item*.cfg files found")
        return 0
    
    print(f"Found {len(item_files)} old item configuration files:")
    for item_file in sorted(item_files):
        print(f"  {item_file}")
    
    # Check if heaters directory exists and has files
    heaters_dir = data_dir / "heaters"
    if not heaters_dir.exists():
        print("\nWARNING: heaters/ directory not found!")
        print("Migration may not have completed successfully.")
        return 1
    
    heater_files = list(heaters_dir.glob("*.cfg"))
    if not heater_files:
        print("\nWARNING: No property files found in heaters/ directory!")
        print("Migration may not have completed successfully.")
        return 1
    
    print(f"\nFound {len(heater_files)} property files in heaters/ directory")
    
    # Ask for confirmation
    print("\nThis will permanently delete the old item*.cfg files.")
    print("Make sure you have:")
    print("1. Tested the new per-property format with your firmware")
    print("2. Verified all heater configurations work correctly")
    print("3. Made a backup if needed")
    
    response = input("\nDo you want to proceed with cleanup? (yes/no): ").lower().strip()
    
    if response not in ['yes', 'y']:
        print("Cleanup cancelled.")
        return 0
    
    # Delete old files
    deleted_count = 0
    for item_file in item_files:
        try:
            item_file.unlink()
            print(f"Deleted: {item_file}")
            deleted_count += 1
        except Exception as e:
            print(f"Error deleting {item_file}: {e}")
    
    print(f"\nCleanup complete: {deleted_count}/{len(item_files)} files deleted")
    
    if deleted_count == len(item_files):
        print("All old configuration files have been removed.")
        print("Your system is now using the new per-property format.")
        return 0
    else:
        print("Some files could not be deleted. Check the errors above.")
        return 1

if __name__ == "__main__":
    sys.exit(main())
