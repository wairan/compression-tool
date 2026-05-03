function ResultCard({ result, isDecompress }) {
  if (!result.success) {
    return (
      <div className="result-card error">
        <p className="result-title">Error</p>
        <p>{result.error}</p>
        {result.details && (
          <div className="error-details">{result.details}</div>
        )}
      </div>
    );
  }

  const formatSize = (bytes) => {
    if (bytes === 0) return '0 Bytes';
    const k = 1024;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  };

  const ratio = isDecompress 
    ? (result.compressedSize / result.originalSize).toFixed(2) + 'x expansion'
    : ((result.originalSize / result.compressedSize).toFixed(2)) + ':1';

  const handleDownload = () => {
    const url = window.URL.createObjectURL(result.blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = result.filename;
    document.body.appendChild(a);
    a.click();
    window.URL.revokeObjectURL(url);
    document.body.removeChild(a);
  };

  return (
    <div className="result-card">
      <p className="result-title">Success!</p>
      <div className="result-stats">
        <div>
          <div className="stat-label">Original Size</div>
          <div className="stat-value">{formatSize(result.originalSize)}</div>
        </div>
        <div>
          <div className="stat-label">{isDecompress ? 'Decompressed Size' : 'Compressed Size'}</div>
          <div className="stat-value">{formatSize(result.compressedSize)}</div>
        </div>
        <div>
          <div className="stat-label">Ratio</div>
          <div className="stat-value">{ratio}</div>
        </div>
        <div>
          <div className="stat-label">Time Taken</div>
          <div className="stat-value">{result.timeTaken} s</div>
        </div>
      </div>
      <button className="download-button" onClick={handleDownload}>
        Download File
      </button>
    </div>
  );
}

export default ResultCard;
