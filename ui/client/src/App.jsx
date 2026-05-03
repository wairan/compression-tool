import { useState, useEffect } from 'react';
import DropZone from './components/DropZone';
import OptionsPanel from './components/OptionsPanel';
import ActionButton from './components/ActionButton';
import ResultCard from './components/ResultCard';

function App() {
  const [file, setFile] = useState(null);
  const [isDecompress, setIsDecompress] = useState(false);
  const [algo, setAlgo] = useState('auto');
  const [threads, setThreads] = useState(navigator.hardwareConcurrency || 4);
  const [isProcessing, setIsProcessing] = useState(false);
  const [result, setResult] = useState(null);
  const [serverHealth, setServerHealth] = useState({ checked: false, ok: false, error: '' });

  useEffect(() => {
    // Check backend health on load
    fetch('http://localhost:3001/health')
      .then(res => res.json().then(data => ({ status: res.status, data })))
      .then(({ status, data }) => {
        if (status === 200) {
          setServerHealth({ checked: true, ok: true, error: '' });
        } else {
          setServerHealth({ checked: true, ok: false, error: data.error || 'Backend error' });
        }
      })
      .catch(err => {
        setServerHealth({ checked: true, ok: false, error: 'Cannot connect to backend server' });
      });
  }, []);

  const handleProcess = async () => {
    if (!file) return;

    setIsProcessing(true);
    setResult(null);

    const formData = new FormData();
    formData.append('file', file);
    formData.append('algo', algo);
    formData.append('threads', threads);
    formData.append('decompress', isDecompress);

    const startTime = performance.now();

    try {
      const response = await fetch('http://localhost:3001/compress', {
        method: 'POST',
        body: formData,
      });

      if (response.ok) {
        const blob = await response.blob();
        
        // Extract filename from Content-Disposition if present
        let filename = isDecompress 
          ? (file.name.endsWith('.cmp') ? file.name.slice(0, -4) : 'decompressed_' + file.name)
          : file.name + '.cmp';

        const disposition = response.headers.get('Content-Disposition');
        if (disposition && disposition.indexOf('filename=') !== -1) {
            const matches = /filename[^;=\n]*=((['"]).*?\2|[^;\n]*)/.exec(disposition);
            if (matches != null && matches[1]) { 
              filename = matches[1].replace(/['"]/g, '');
            }
        }

        const endTime = performance.now();
        const timeTaken = ((endTime - startTime) / 1000).toFixed(2);
        
        setResult({
          success: true,
          originalSize: file.size,
          compressedSize: blob.size,
          timeTaken,
          blob,
          filename
        });
      } else {
        const errorData = await response.json();
        setResult({
          success: false,
          error: errorData.error,
          details: errorData.details
        });
      }
    } catch (err) {
      setResult({
        success: false,
        error: 'Network error',
        details: err.message
      });
    } finally {
      setIsProcessing(false);
    }
  };

  return (
    <div className="container">
      <h1>FileCompressor</h1>
      
      {!serverHealth.checked ? (
        <p>Checking server...</p>
      ) : !serverHealth.ok ? (
        <div className="result-card error">
          <p className="result-title">Backend Error</p>
          <p>{serverHealth.error}</p>
          <p className="error-details">Ensure the C++ binary is compiled and the server is running.</p>
        </div>
      ) : (
        <>
          <div className="form-group">
            <DropZone 
              file={file} 
              onFileSelect={setFile} 
              disabled={isProcessing} 
            />
          </div>

          <div className="form-group">
            <OptionsPanel
              isDecompress={isDecompress}
              setIsDecompress={setIsDecompress}
              algo={algo}
              setAlgo={setAlgo}
              threads={threads}
              setThreads={setThreads}
              disabled={isProcessing}
            />
          </div>

          <div className="form-group">
            <ActionButton 
              onClick={handleProcess}
              disabled={!file || isProcessing}
              isProcessing={isProcessing}
              label={isDecompress ? "Decompress File" : "Compress File"}
            />
          </div>

          {result && <ResultCard result={result} isDecompress={isDecompress} />}
        </>
      )}
    </div>
  );
}

export default App;
