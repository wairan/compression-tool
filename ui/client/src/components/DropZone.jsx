import { useRef, useState } from 'react';

// 500 MB max file size
const MAX_FILE_SIZE = 500 * 1024 * 1024; 

function DropZone({ file, onFileSelect, disabled }) {
  const [isDragActive, setIsDragActive] = useState(false);
  const [error, setError] = useState('');
  const inputRef = useRef(null);

  const handleFile = (selectedFile) => {
    if (!selectedFile) return;
    
    if (selectedFile.size > MAX_FILE_SIZE) {
      setError(`File is too large. Maximum size is 500 MB.`);
      onFileSelect(null);
    } else {
      setError('');
      onFileSelect(selectedFile);
    }
  };

  const onDragOver = (e) => {
    e.preventDefault();
    if (!disabled) setIsDragActive(true);
  };

  const onDragLeave = (e) => {
    e.preventDefault();
    setIsDragActive(false);
  };

  const onDrop = (e) => {
    e.preventDefault();
    setIsDragActive(false);
    if (disabled) return;
    
    if (e.dataTransfer.files && e.dataTransfer.files.length > 0) {
      handleFile(e.dataTransfer.files[0]);
    }
  };

  const onClick = () => {
    if (!disabled && inputRef.current) {
      inputRef.current.click();
    }
  };

  const onFileChange = (e) => {
    if (e.target.files && e.target.files.length > 0) {
      handleFile(e.target.files[0]);
    }
  };

  const formatSize = (bytes) => {
    if (bytes === 0) return '0 Bytes';
    const k = 1024;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  };

  return (
    <>
      <span className="label">File Selection</span>
      <div 
        className={`dropzone ${isDragActive ? 'active' : ''} ${disabled ? 'disabled' : ''}`}
        onDragOver={onDragOver}
        onDragLeave={onDragLeave}
        onDrop={onDrop}
        onClick={onClick}
      >
        <input 
          type="file" 
          ref={inputRef} 
          onChange={onFileChange} 
          className="hidden-input" 
          disabled={disabled}
        />
        
        {file ? (
          <div>
            <p className="dropzone-text">Selected File:</p>
            <p className="dropzone-file">{file.name} ({formatSize(file.size)})</p>
          </div>
        ) : (
          <p className="dropzone-text">
            Drag & drop a file here, or click to browse
          </p>
        )}
      </div>
      {error && <p className="dropzone-error">{error}</p>}
    </>
  );
}

export default DropZone;
